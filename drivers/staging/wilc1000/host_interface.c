#include <linux/slab.h>
#include <linux/time.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include "host_interface.h"
#include "coreconfigurator.h"
#include "wilc_wlan_if.h"
#include "wilc_msgqueue.h"
#include <linux/etherdevice.h>

extern u8 connecting;

extern struct timer_list hDuringIpTimer;

extern u8 g_wilc_initialized;

#define HOST_IF_MSG_SCAN                        0
#define HOST_IF_MSG_CONNECT                     1
#define HOST_IF_MSG_RCVD_GNRL_ASYNC_INFO        2
#define HOST_IF_MSG_KEY                         3
#define HOST_IF_MSG_RCVD_NTWRK_INFO             4
#define HOST_IF_MSG_RCVD_SCAN_COMPLETE          5
#define HOST_IF_MSG_CFG_PARAMS                  6
#define HOST_IF_MSG_SET_CHANNEL                 7
#define HOST_IF_MSG_DISCONNECT                  8
#define HOST_IF_MSG_GET_RSSI                    9
#define HOST_IF_MSG_GET_CHNL                    10
#define HOST_IF_MSG_ADD_BEACON                  11
#define HOST_IF_MSG_DEL_BEACON                  12
#define HOST_IF_MSG_ADD_STATION                 13
#define HOST_IF_MSG_DEL_STATION                 14
#define HOST_IF_MSG_EDIT_STATION                15
#define HOST_IF_MSG_SCAN_TIMER_FIRED            16
#define HOST_IF_MSG_CONNECT_TIMER_FIRED         17
#define HOST_IF_MSG_POWER_MGMT                  18
#define HOST_IF_MSG_GET_INACTIVETIME            19
#define HOST_IF_MSG_REMAIN_ON_CHAN              20
#define HOST_IF_MSG_REGISTER_FRAME              21
#define HOST_IF_MSG_LISTEN_TIMER_FIRED          22
#define HOST_IF_MSG_GET_LINKSPEED               23
#define HOST_IF_MSG_SET_WFIDRV_HANDLER          24
#define HOST_IF_MSG_SET_MAC_ADDRESS             25
#define HOST_IF_MSG_GET_MAC_ADDRESS             26
#define HOST_IF_MSG_SET_OPERATION_MODE          27
#define HOST_IF_MSG_SET_IPADDRESS               28
#define HOST_IF_MSG_GET_IPADDRESS               29
#define HOST_IF_MSG_FLUSH_CONNECT               30
#define HOST_IF_MSG_GET_STATISTICS              31
#define HOST_IF_MSG_SET_MULTICAST_FILTER        32
#define HOST_IF_MSG_ADD_BA_SESSION              33
#define HOST_IF_MSG_DEL_BA_SESSION              34
#define HOST_IF_MSG_Q_IDLE                      35
#define HOST_IF_MSG_DEL_ALL_STA                 36
#define HOST_IF_MSG_DEL_ALL_RX_BA_SESSIONS      34
#define HOST_IF_MSG_EXIT                        100

#define HOST_IF_SCAN_TIMEOUT                    4000
#define HOST_IF_CONNECT_TIMEOUT                 9500

#define BA_SESSION_DEFAULT_BUFFER_SIZE          16
#define BA_SESSION_DEFAULT_TIMEOUT              1000
#define BLOCK_ACK_REQ_SIZE                      0x14

struct cfg_param_attr {
	struct cfg_param_val cfg_attr_info;
};

struct host_if_wpa_attr {
	u8 *key;
	const u8 *mac_addr;
	u8 *seq;
	u8 seq_len;
	u8 index;
	u8 key_len;
	u8 mode;
};

struct host_if_wep_attr {
	u8 *key;
	u8 key_len;
	u8 index;
	u8 mode;
	enum AUTHTYPE auth_type;
};

union host_if_key_attr {
	struct host_if_wep_attr wep;
	struct host_if_wpa_attr wpa;
	struct host_if_pmkid_attr pmkid;
};

struct key_attr {
	enum KEY_TYPE type;
	u8 action;
	union host_if_key_attr attr;
};

struct scan_attr {
	u8 src;
	u8 type;
	u8 *ch_freq_list;
	u8 ch_list_len;
	u8 *ies;
	size_t ies_len;
	wilc_scan_result result;
	void *arg;
	struct hidden_network hidden_network;
};

struct connect_attr {
	u8 *bssid;
	u8 *ssid;
	size_t ssid_len;
	u8 *ies;
	size_t ies_len;
	u8 security;
	wilc_connect_result result;
	void *arg;
	enum AUTHTYPE auth_type;
	u8 ch;
	void *params;
};

struct rcvd_async_info {
	u8 *buffer;
	u32 len;
};

struct channel_attr {
	u8 set_ch;
};

struct beacon_attr {
	u32 interval;
	u32 dtim_period;
	u32 head_len;
	u8 *head;
	u32 tail_len;
	u8 *tail;
};

struct set_multicast {
	bool bIsEnabled;
	u32 u32count;
};

struct del_all_sta {
	u8 au8Sta_DelAllSta[MAX_NUM_STA][ETH_ALEN];
	u8 u8Num_AssocSta;
};

struct del_sta {
	u8 au8MacAddr[ETH_ALEN];
};

struct power_mgmt_param {

	bool bIsEnabled;
	u32 u32Timeout;
};

struct set_ip_addr {
	u8 *au8IPAddr;
	u8 idx;
};

struct sta_inactive_t {
	u8 mac[6];
};

union message_body {
	struct scan_attr scan_info;
	struct connect_attr con_info;
	struct rcvd_net_info net_info;
	struct rcvd_async_info async_info;
	struct key_attr key_info;
	struct cfg_param_attr cfg_info;
	struct channel_attr channel_info;
	struct beacon_attr beacon_info;
	struct add_sta_param add_sta_info;
	struct del_sta del_sta_info;
	struct add_sta_param edit_sta_info;
	struct power_mgmt_param pwr_mgmt_info;
	struct sta_inactive_t mac_info;
	struct set_ip_addr ip_info;
	struct drv_handler drv;
	struct set_multicast multicast_info;
	struct op_mode mode;
	struct set_mac_addr set_mac_info;
	struct get_mac_addr get_mac_info;
	struct ba_session_info session_info;
	struct remain_ch remain_on_ch;
	struct reg_frame reg_frame;
	char *data;
	struct del_all_sta del_all_sta_info;
};

struct host_if_msg {
	u16 id;
	union message_body body;
	struct host_if_drv *drv;
};

struct join_bss_param {
	BSSTYPE_T bss_type;
	u8 dtim_period;
	u16 beacon_period;
	u16 cap_info;
	u8 au8bssid[6];
	char ssid[MAX_SSID_LEN];
	u8 ssidLen;
	u8 supp_rates[MAX_RATES_SUPPORTED + 1];
	u8 ht_capable;
	u8 wmm_cap;
	u8 uapsd_cap;
	bool rsn_found;
	u8 rsn_grp_policy;
	u8 mode_802_11i;
	u8 rsn_pcip_policy[3];
	u8 rsn_auth_policy[3];
	u8 rsn_cap[2];
	u32 tsf;
	u8 u8NoaEnbaled;
	u8 u8OppEnable;
	u8 u8CtWindow;
	u8 u8Count;
	u8 u8Index;
	u8 au8Duration[4];
	u8 au8Interval[4];
	u8 au8StartTime[4];
};

enum scan_conn_timer {
	SCAN_TIMER = 0,
	CONNECT_TIMER	= 1,
	SCAN_CONNECT_TIMER_FORCE_32BIT = 0xFFFFFFFF
};

static struct host_if_drv *wfidrv_list[NUM_CONCURRENT_IFC + 1];
struct host_if_drv *terminated_handle;
struct host_if_drv *gWFiDrvHandle;
bool g_obtainingIP;
u8 P2P_LISTEN_STATE;
static struct task_struct *HostIFthreadHandler;
static WILC_MsgQueueHandle gMsgQHostIF;
static struct semaphore hSemHostIFthrdEnd;

struct semaphore hSemDeinitDrvHandle;
static struct semaphore hWaitResponse;
struct semaphore hSemHostIntDeinit;
struct timer_list g_hPeriodicRSSI;



u8 gau8MulticastMacAddrList[WILC_MULTICAST_TABLE_SIZE][ETH_ALEN];

static u8 gapu8RcvdAssocResp[MAX_ASSOC_RESP_FRAME_SIZE];

bool gbScanWhileConnected;

static s8 gs8Rssi;
static s8 gs8lnkspd;
static u8 gu8Chnl;
static u8 gs8SetIP[2][4];
static u8 gs8GetIP[2][4];
static u32 gu32InactiveTime;
static u8 gu8DelBcn;
static u32 gu32WidConnRstHack;

u8 *gu8FlushedJoinReq;
u8 *gu8FlushedInfoElemAsoc;
u8 gu8Flushed11iMode;
u8 gu8FlushedAuthType;
u32 gu32FlushedJoinReqSize;
u32 gu32FlushedInfoElemAsocSize;
struct host_if_drv *gu8FlushedJoinReqDrvHandler;
#define REAL_JOIN_REQ 0
#define FLUSHED_JOIN_REQ 1
#define FLUSHED_BYTE_POS 79

static void *host_int_ParseJoinBssParam(tstrNetworkInfo *ptstrNetworkInfo);

extern void chip_sleep_manually(u32 u32SleepTime);
extern int linux_wlan_get_num_conn_ifcs(void);

static int add_handler_in_list(struct host_if_drv *handler)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(wfidrv_list); i++) {
		if (!wfidrv_list[i]) {
			wfidrv_list[i] = handler;
			return 0;
		}
	}

	return -ENOBUFS;
}

static int remove_handler_in_list(struct host_if_drv *handler)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(wfidrv_list); i++) {
		if (wfidrv_list[i] == handler) {
			wfidrv_list[i] = NULL;
			return 0;
		}
	}

	return -EINVAL;
}

static int get_id_from_handler(struct host_if_drv *handler)
{
	int i;

	if (!handler)
		return 0;

	for (i = 1; i < ARRAY_SIZE(wfidrv_list); i++) {
		if (wfidrv_list[i] == handler)
			return i;
	}

	return 0;
}

static struct host_if_drv *get_handler_from_id(int id)
{
	if (id <= 0 || id >= ARRAY_SIZE(wfidrv_list))
		return NULL;
	return wfidrv_list[id];
}

static s32 Handle_SetChannel(struct host_if_drv *hif_drv,
			     struct channel_attr *pstrHostIFSetChan)
{

	s32 s32Error = 0;
	struct wid strWID;

	strWID.id = (u16)WID_CURRENT_CHANNEL;
	strWID.type = WID_CHAR;
	strWID.val = (char *)&(pstrHostIFSetChan->set_ch);
	strWID.size = sizeof(char);

	PRINT_D(HOSTINF_DBG, "Setting channel\n");

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error) {
		PRINT_ER("Failed to set channel\n");
		return -EINVAL;
	}

	return s32Error;
}

static s32 Handle_SetWfiDrvHandler(struct host_if_drv *hif_drv,
				   struct drv_handler *pstrHostIfSetDrvHandler)
{

	s32 s32Error = 0;
	struct wid strWID;

	strWID.id = (u16)WID_SET_DRV_HANDLER;
	strWID.type = WID_INT;
	strWID.val = (s8 *)&(pstrHostIfSetDrvHandler->u32Address);
	strWID.size = sizeof(u32);

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   pstrHostIfSetDrvHandler->u32Address);

	if (!hif_drv)
		up(&hSemDeinitDrvHandle);


	if (s32Error) {
		PRINT_ER("Failed to set driver handler\n");
		return -EINVAL;
	}

	return s32Error;
}

static s32 Handle_SetOperationMode(struct host_if_drv *hif_drv,
				   struct op_mode *pstrHostIfSetOperationMode)
{

	s32 s32Error = 0;
	struct wid strWID;

	strWID.id = (u16)WID_SET_OPERATION_MODE;
	strWID.type = WID_INT;
	strWID.val = (s8 *)&(pstrHostIfSetOperationMode->u32Mode);
	strWID.size = sizeof(u32);

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));


	if ((pstrHostIfSetOperationMode->u32Mode) == IDLE_MODE)
		up(&hSemDeinitDrvHandle);


	if (s32Error) {
		PRINT_ER("Failed to set driver handler\n");
		return -EINVAL;
	}

	return s32Error;
}

s32 Handle_set_IPAddress(struct host_if_drv *hif_drv, u8 *pu8IPAddr, u8 idx)
{

	s32 s32Error = 0;
	struct wid strWID;
	char firmwareIPAddress[4] = {0};

	if (pu8IPAddr[0] < 192)
		pu8IPAddr[0] = 0;

	PRINT_INFO(HOSTINF_DBG, "Indx = %d, Handling set  IP = %pI4\n", idx, pu8IPAddr);

	memcpy(gs8SetIP[idx], pu8IPAddr, IP_ALEN);

	strWID.id = (u16)WID_IP_ADDRESS;
	strWID.type = WID_STR;
	strWID.val = (u8 *)pu8IPAddr;
	strWID.size = IP_ALEN;

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));


	host_int_get_ipaddress(hif_drv, firmwareIPAddress, idx);

	if (s32Error) {
		PRINT_ER("Failed to set IP address\n");
		return -EINVAL;
	}

	PRINT_INFO(HOSTINF_DBG, "IP address set\n");

	return s32Error;
}

s32 Handle_get_IPAddress(struct host_if_drv *hif_drv, u8 *pu8IPAddr, u8 idx)
{

	s32 s32Error = 0;
	struct wid strWID;

	strWID.id = (u16)WID_IP_ADDRESS;
	strWID.type = WID_STR;
	strWID.val = kmalloc(IP_ALEN, GFP_KERNEL);
	strWID.size = IP_ALEN;

	s32Error = send_config_pkt(GET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));

	PRINT_INFO(HOSTINF_DBG, "%pI4\n", strWID.val);

	memcpy(gs8GetIP[idx], strWID.val, IP_ALEN);

	kfree(strWID.val);

	if (memcmp(gs8GetIP[idx], gs8SetIP[idx], IP_ALEN) != 0)
		host_int_setup_ipaddress(hif_drv, gs8SetIP[idx], idx);

	if (s32Error != 0) {
		PRINT_ER("Failed to get IP address\n");
		return -EINVAL;
	}

	PRINT_INFO(HOSTINF_DBG, "IP address retrieved:: u8IfIdx = %d\n", idx);
	PRINT_INFO(HOSTINF_DBG, "%pI4\n", gs8GetIP[idx]);
	PRINT_INFO(HOSTINF_DBG, "\n");

	return s32Error;
}

static s32 Handle_SetMacAddress(struct host_if_drv *hif_drv,
				struct set_mac_addr *pstrHostIfSetMacAddress)
{

	s32 s32Error = 0;
	struct wid strWID;
	u8 *mac_buf = kmalloc(ETH_ALEN, GFP_KERNEL);

	if (mac_buf == NULL) {
		PRINT_ER("No buffer to send mac address\n");
		return -EFAULT;
	}
	memcpy(mac_buf, pstrHostIfSetMacAddress->u8MacAddress, ETH_ALEN);

	strWID.id = (u16)WID_MAC_ADDR;
	strWID.type = WID_STR;
	strWID.val = mac_buf;
	strWID.size = ETH_ALEN;
	PRINT_D(GENERIC_DBG, "mac addr = :%pM\n", strWID.val);

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error) {
		PRINT_ER("Failed to set mac address\n");
		s32Error = -EFAULT;
	}

	kfree(mac_buf);
	return s32Error;
}

static s32 Handle_GetMacAddress(struct host_if_drv *hif_drv,
				struct get_mac_addr *pstrHostIfGetMacAddress)
{

	s32 s32Error = 0;
	struct wid strWID;

	strWID.id = (u16)WID_MAC_ADDR;
	strWID.type = WID_STR;
	strWID.val = pstrHostIfGetMacAddress->u8MacAddress;
	strWID.size = ETH_ALEN;

	s32Error = send_config_pkt(GET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error) {
		PRINT_ER("Failed to get mac address\n");
		s32Error = -EFAULT;
	}
	up(&hWaitResponse);

	return s32Error;
}

static s32 Handle_CfgParam(struct host_if_drv *hif_drv,
			   struct cfg_param_attr *strHostIFCfgParamAttr)
{
	s32 s32Error = 0;
	struct wid strWIDList[32];
	u8 u8WidCnt = 0;

	down(&hif_drv->gtOsCfgValuesSem);


	PRINT_D(HOSTINF_DBG, "Setting CFG params\n");

	if (strHostIFCfgParamAttr->cfg_attr_info.flag & BSS_TYPE) {
		if (strHostIFCfgParamAttr->cfg_attr_info.bss_type < 6) {
			strWIDList[u8WidCnt].id = WID_BSS_TYPE;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.bss_type;
			strWIDList[u8WidCnt].type = WID_CHAR;
			strWIDList[u8WidCnt].size = sizeof(char);
			hif_drv->strCfgValues.bss_type = (u8)strHostIFCfgParamAttr->cfg_attr_info.bss_type;
		} else {
			PRINT_ER("check value 6 over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & AUTH_TYPE) {
		if ((strHostIFCfgParamAttr->cfg_attr_info.auth_type) == 1 || (strHostIFCfgParamAttr->cfg_attr_info.auth_type) == 2 || (strHostIFCfgParamAttr->cfg_attr_info.auth_type) == 5) {
			strWIDList[u8WidCnt].id = WID_AUTH_TYPE;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.auth_type;
			strWIDList[u8WidCnt].type = WID_CHAR;
			strWIDList[u8WidCnt].size = sizeof(char);
			hif_drv->strCfgValues.auth_type = (u8)strHostIFCfgParamAttr->cfg_attr_info.auth_type;
		} else {
			PRINT_ER("Impossible value \n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & AUTHEN_TIMEOUT) {
		if (strHostIFCfgParamAttr->cfg_attr_info.auth_timeout > 0 && strHostIFCfgParamAttr->cfg_attr_info.auth_timeout < 65536) {
			strWIDList[u8WidCnt].id = WID_AUTH_TIMEOUT;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.auth_timeout;
			strWIDList[u8WidCnt].type = WID_SHORT;
			strWIDList[u8WidCnt].size = sizeof(u16);
			hif_drv->strCfgValues.auth_timeout = strHostIFCfgParamAttr->cfg_attr_info.auth_timeout;
		} else {
			PRINT_ER("Range(1 ~ 65535) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & POWER_MANAGEMENT) {
		if (strHostIFCfgParamAttr->cfg_attr_info.power_mgmt_mode < 5) {
			strWIDList[u8WidCnt].id = WID_POWER_MANAGEMENT;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.power_mgmt_mode;
			strWIDList[u8WidCnt].type = WID_CHAR;
			strWIDList[u8WidCnt].size = sizeof(char);
			hif_drv->strCfgValues.power_mgmt_mode = (u8)strHostIFCfgParamAttr->cfg_attr_info.power_mgmt_mode;
		} else {
			PRINT_ER("Invalide power mode\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & RETRY_SHORT) {
		if ((strHostIFCfgParamAttr->cfg_attr_info.short_retry_limit > 0) && (strHostIFCfgParamAttr->cfg_attr_info.short_retry_limit < 256))	{
			strWIDList[u8WidCnt].id = WID_SHORT_RETRY_LIMIT;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.short_retry_limit;
			strWIDList[u8WidCnt].type = WID_SHORT;
			strWIDList[u8WidCnt].size = sizeof(u16);
			hif_drv->strCfgValues.short_retry_limit = strHostIFCfgParamAttr->cfg_attr_info.short_retry_limit;
		} else {
			PRINT_ER("Range(1~256) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & RETRY_LONG) {
		if ((strHostIFCfgParamAttr->cfg_attr_info.long_retry_limit > 0) && (strHostIFCfgParamAttr->cfg_attr_info.long_retry_limit < 256)) {
			strWIDList[u8WidCnt].id = WID_LONG_RETRY_LIMIT;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.long_retry_limit;

			strWIDList[u8WidCnt].type = WID_SHORT;
			strWIDList[u8WidCnt].size = sizeof(u16);
			hif_drv->strCfgValues.long_retry_limit = strHostIFCfgParamAttr->cfg_attr_info.long_retry_limit;
		} else {
			PRINT_ER("Range(1~256) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & FRAG_THRESHOLD) {

		if (strHostIFCfgParamAttr->cfg_attr_info.frag_threshold > 255 && strHostIFCfgParamAttr->cfg_attr_info.frag_threshold < 7937) {
			strWIDList[u8WidCnt].id = WID_FRAG_THRESHOLD;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.frag_threshold;
			strWIDList[u8WidCnt].type = WID_SHORT;
			strWIDList[u8WidCnt].size = sizeof(u16);
			hif_drv->strCfgValues.frag_threshold = strHostIFCfgParamAttr->cfg_attr_info.frag_threshold;
		} else {
			PRINT_ER("Threshold Range fail\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & RTS_THRESHOLD) {
		if (strHostIFCfgParamAttr->cfg_attr_info.rts_threshold > 255 && strHostIFCfgParamAttr->cfg_attr_info.rts_threshold < 65536)	{
			strWIDList[u8WidCnt].id = WID_RTS_THRESHOLD;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.rts_threshold;
			strWIDList[u8WidCnt].type = WID_SHORT;
			strWIDList[u8WidCnt].size = sizeof(u16);
			hif_drv->strCfgValues.rts_threshold = strHostIFCfgParamAttr->cfg_attr_info.rts_threshold;
		} else {
			PRINT_ER("Threshold Range fail\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & PREAMBLE) {
		if (strHostIFCfgParamAttr->cfg_attr_info.preamble_type < 3) {
			strWIDList[u8WidCnt].id = WID_PREAMBLE;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.preamble_type;
			strWIDList[u8WidCnt].type = WID_CHAR;
			strWIDList[u8WidCnt].size = sizeof(char);
			hif_drv->strCfgValues.preamble_type = strHostIFCfgParamAttr->cfg_attr_info.preamble_type;
		} else {
			PRINT_ER("Preamle Range(0~2) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & SHORT_SLOT_ALLOWED) {
		if (strHostIFCfgParamAttr->cfg_attr_info.short_slot_allowed < 2) {
			strWIDList[u8WidCnt].id = WID_SHORT_SLOT_ALLOWED;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.short_slot_allowed;
			strWIDList[u8WidCnt].type = WID_CHAR;
			strWIDList[u8WidCnt].size = sizeof(char);
			hif_drv->strCfgValues.short_slot_allowed = (u8)strHostIFCfgParamAttr->cfg_attr_info.short_slot_allowed;
		} else {
			PRINT_ER("Short slot(2) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & TXOP_PROT_DISABLE) {
		if (strHostIFCfgParamAttr->cfg_attr_info.txop_prot_disabled < 2) {
			strWIDList[u8WidCnt].id = WID_11N_TXOP_PROT_DISABLE;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.txop_prot_disabled;
			strWIDList[u8WidCnt].type = WID_CHAR;
			strWIDList[u8WidCnt].size = sizeof(char);
			hif_drv->strCfgValues.txop_prot_disabled = (u8)strHostIFCfgParamAttr->cfg_attr_info.txop_prot_disabled;
		} else {
			PRINT_ER("TXOP prot disable\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & BEACON_INTERVAL) {
		if (strHostIFCfgParamAttr->cfg_attr_info.beacon_interval > 0 && strHostIFCfgParamAttr->cfg_attr_info.beacon_interval < 65536) {
			strWIDList[u8WidCnt].id = WID_BEACON_INTERVAL;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.beacon_interval;
			strWIDList[u8WidCnt].type = WID_SHORT;
			strWIDList[u8WidCnt].size = sizeof(u16);
			hif_drv->strCfgValues.beacon_interval = strHostIFCfgParamAttr->cfg_attr_info.beacon_interval;
		} else {
			PRINT_ER("Beacon interval(1~65535) fail\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & DTIM_PERIOD) {
		if (strHostIFCfgParamAttr->cfg_attr_info.dtim_period > 0 && strHostIFCfgParamAttr->cfg_attr_info.dtim_period < 256) {
			strWIDList[u8WidCnt].id = WID_DTIM_PERIOD;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.dtim_period;
			strWIDList[u8WidCnt].type = WID_CHAR;
			strWIDList[u8WidCnt].size = sizeof(char);
			hif_drv->strCfgValues.dtim_period = strHostIFCfgParamAttr->cfg_attr_info.dtim_period;
		} else {
			PRINT_ER("DTIM range(1~255) fail\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & SITE_SURVEY) {
		if (strHostIFCfgParamAttr->cfg_attr_info.site_survey_enabled < 3) {
			strWIDList[u8WidCnt].id = WID_SITE_SURVEY;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.site_survey_enabled;
			strWIDList[u8WidCnt].type = WID_CHAR;
			strWIDList[u8WidCnt].size = sizeof(char);
			hif_drv->strCfgValues.site_survey_enabled = (u8)strHostIFCfgParamAttr->cfg_attr_info.site_survey_enabled;
		} else {
			PRINT_ER("Site survey disable\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & SITE_SURVEY_SCAN_TIME) {
		if (strHostIFCfgParamAttr->cfg_attr_info.site_survey_scan_time > 0 && strHostIFCfgParamAttr->cfg_attr_info.site_survey_scan_time < 65536) {
			strWIDList[u8WidCnt].id = WID_SITE_SURVEY_SCAN_TIME;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.site_survey_scan_time;
			strWIDList[u8WidCnt].type = WID_SHORT;
			strWIDList[u8WidCnt].size = sizeof(u16);
			hif_drv->strCfgValues.site_survey_scan_time = strHostIFCfgParamAttr->cfg_attr_info.site_survey_scan_time;
		} else {
			PRINT_ER("Site survey scan time(1~65535) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & ACTIVE_SCANTIME) {
		if (strHostIFCfgParamAttr->cfg_attr_info.active_scan_time > 0 && strHostIFCfgParamAttr->cfg_attr_info.active_scan_time < 65536) {
			strWIDList[u8WidCnt].id = WID_ACTIVE_SCAN_TIME;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.active_scan_time;
			strWIDList[u8WidCnt].type = WID_SHORT;
			strWIDList[u8WidCnt].size = sizeof(u16);
			hif_drv->strCfgValues.active_scan_time = strHostIFCfgParamAttr->cfg_attr_info.active_scan_time;
		} else {
			PRINT_ER("Active scan time(1~65535) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & PASSIVE_SCANTIME) {
		if (strHostIFCfgParamAttr->cfg_attr_info.passive_scan_time > 0 && strHostIFCfgParamAttr->cfg_attr_info.passive_scan_time < 65536) {
			strWIDList[u8WidCnt].id = WID_PASSIVE_SCAN_TIME;
			strWIDList[u8WidCnt].val = (s8 *)&strHostIFCfgParamAttr->cfg_attr_info.passive_scan_time;
			strWIDList[u8WidCnt].type = WID_SHORT;
			strWIDList[u8WidCnt].size = sizeof(u16);
			hif_drv->strCfgValues.passive_scan_time = strHostIFCfgParamAttr->cfg_attr_info.passive_scan_time;
		} else {
			PRINT_ER("Passive scan time(1~65535) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->cfg_attr_info.flag & CURRENT_TX_RATE) {
		enum CURRENT_TXRATE curr_tx_rate = strHostIFCfgParamAttr->cfg_attr_info.curr_tx_rate;
		if (curr_tx_rate == AUTORATE || curr_tx_rate == MBPS_1
		    || curr_tx_rate == MBPS_2 || curr_tx_rate == MBPS_5_5
		    || curr_tx_rate == MBPS_11 || curr_tx_rate == MBPS_6
		    || curr_tx_rate == MBPS_9 || curr_tx_rate == MBPS_12
		    || curr_tx_rate == MBPS_18 || curr_tx_rate == MBPS_24
		    || curr_tx_rate == MBPS_36 || curr_tx_rate == MBPS_48 || curr_tx_rate == MBPS_54) {
			strWIDList[u8WidCnt].id = WID_CURRENT_TX_RATE;
			strWIDList[u8WidCnt].val = (s8 *)&curr_tx_rate;
			strWIDList[u8WidCnt].type = WID_SHORT;
			strWIDList[u8WidCnt].size = sizeof(u16);
			hif_drv->strCfgValues.curr_tx_rate = (u8)curr_tx_rate;
		} else {
			PRINT_ER("out of TX rate\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	s32Error = send_config_pkt(SET_CFG, strWIDList, u8WidCnt,
				   get_id_from_handler(hif_drv));

	if (s32Error)
		PRINT_ER("Error in setting CFG params\n");

ERRORHANDLER:
	up(&hif_drv->gtOsCfgValuesSem);
	return s32Error;
}

static s32 Handle_wait_msg_q_empty(void)
{
	g_wilc_initialized = 0;
	up(&hWaitResponse);
	return 0;
}

static s32 Handle_Scan(struct host_if_drv *hif_drv,
		       struct scan_attr *pstrHostIFscanAttr)
{
	s32 s32Error = 0;
	struct wid strWIDList[5];
	u32 u32WidsCount = 0;
	u32 i;
	u8 *pu8Buffer;
	u8 valuesize = 0;
	u8 *pu8HdnNtwrksWidVal = NULL;

	PRINT_D(HOSTINF_DBG, "Setting SCAN params\n");
	PRINT_D(HOSTINF_DBG, "Scanning: In [%d] state\n", hif_drv->enuHostIFstate);

	hif_drv->strWILC_UsrScanReq.pfUserScanResult = pstrHostIFscanAttr->result;
	hif_drv->strWILC_UsrScanReq.u32UserScanPvoid = pstrHostIFscanAttr->arg;

	if ((hif_drv->enuHostIFstate >= HOST_IF_SCANNING) && (hif_drv->enuHostIFstate < HOST_IF_CONNECTED)) {
		PRINT_D(GENERIC_DBG, "Don't scan we are already in [%d] state\n", hif_drv->enuHostIFstate);
		PRINT_ER("Already scan\n");
		s32Error = -EBUSY;
		goto ERRORHANDLER;
	}

	if (g_obtainingIP || connecting) {
		PRINT_D(GENERIC_DBG, "[handle_scan]: Don't do obss scan until IP adresss is obtained\n");
		PRINT_ER("Don't do obss scan\n");
		s32Error = -EBUSY;
		goto ERRORHANDLER;
	}

	PRINT_D(HOSTINF_DBG, "Setting SCAN params\n");


	hif_drv->strWILC_UsrScanReq.u32RcvdChCount = 0;

	strWIDList[u32WidsCount].id = (u16)WID_SSID_PROBE_REQ;
	strWIDList[u32WidsCount].type = WID_STR;

	for (i = 0; i < pstrHostIFscanAttr->hidden_network.u8ssidnum; i++)
		valuesize += ((pstrHostIFscanAttr->hidden_network.pstrHiddenNetworkInfo[i].u8ssidlen) + 1);
	pu8HdnNtwrksWidVal = kmalloc(valuesize + 1, GFP_KERNEL);
	strWIDList[u32WidsCount].val = pu8HdnNtwrksWidVal;
	if (strWIDList[u32WidsCount].val != NULL) {
		pu8Buffer = strWIDList[u32WidsCount].val;

		*pu8Buffer++ = pstrHostIFscanAttr->hidden_network.u8ssidnum;

		PRINT_D(HOSTINF_DBG, "In Handle_ProbeRequest number of ssid %d\n", pstrHostIFscanAttr->hidden_network.u8ssidnum);

		for (i = 0; i < pstrHostIFscanAttr->hidden_network.u8ssidnum; i++) {
			*pu8Buffer++ = pstrHostIFscanAttr->hidden_network.pstrHiddenNetworkInfo[i].u8ssidlen;
			memcpy(pu8Buffer, pstrHostIFscanAttr->hidden_network.pstrHiddenNetworkInfo[i].pu8ssid, pstrHostIFscanAttr->hidden_network.pstrHiddenNetworkInfo[i].u8ssidlen);
			pu8Buffer += pstrHostIFscanAttr->hidden_network.pstrHiddenNetworkInfo[i].u8ssidlen;
		}



		strWIDList[u32WidsCount].size = (s32)(valuesize + 1);
		u32WidsCount++;
	}

	{
		strWIDList[u32WidsCount].id = WID_INFO_ELEMENT_PROBE;
		strWIDList[u32WidsCount].type = WID_BIN_DATA;
		strWIDList[u32WidsCount].val = pstrHostIFscanAttr->ies;
		strWIDList[u32WidsCount].size = pstrHostIFscanAttr->ies_len;
		u32WidsCount++;
	}

	strWIDList[u32WidsCount].id = WID_SCAN_TYPE;
	strWIDList[u32WidsCount].type = WID_CHAR;
	strWIDList[u32WidsCount].size = sizeof(char);
	strWIDList[u32WidsCount].val = (s8 *)(&(pstrHostIFscanAttr->type));
	u32WidsCount++;

	strWIDList[u32WidsCount].id = WID_SCAN_CHANNEL_LIST;
	strWIDList[u32WidsCount].type = WID_BIN_DATA;

	if (pstrHostIFscanAttr->ch_freq_list != NULL && pstrHostIFscanAttr->ch_list_len > 0) {
		int i;

		for (i = 0; i < pstrHostIFscanAttr->ch_list_len; i++)	{
			if (pstrHostIFscanAttr->ch_freq_list[i] > 0)
				pstrHostIFscanAttr->ch_freq_list[i] = pstrHostIFscanAttr->ch_freq_list[i] - 1;
		}
	}

	strWIDList[u32WidsCount].val = pstrHostIFscanAttr->ch_freq_list;
	strWIDList[u32WidsCount].size = pstrHostIFscanAttr->ch_list_len;
	u32WidsCount++;

	strWIDList[u32WidsCount].id = WID_START_SCAN_REQ;
	strWIDList[u32WidsCount].type = WID_CHAR;
	strWIDList[u32WidsCount].size = sizeof(char);
	strWIDList[u32WidsCount].val = (s8 *)(&(pstrHostIFscanAttr->src));
	u32WidsCount++;

	if (hif_drv->enuHostIFstate == HOST_IF_CONNECTED)
		gbScanWhileConnected = true;
	else if (hif_drv->enuHostIFstate == HOST_IF_IDLE)
		gbScanWhileConnected = false;

	s32Error = send_config_pkt(SET_CFG, strWIDList, u32WidsCount,
				   get_id_from_handler(hif_drv));

	if (s32Error)
		PRINT_ER("Failed to send scan paramters config packet\n");
	else
		PRINT_D(HOSTINF_DBG, "Successfully sent SCAN params config packet\n");

ERRORHANDLER:
	if (s32Error) {
		del_timer(&hif_drv->hScanTimer);
		Handle_ScanDone(hif_drv, SCAN_EVENT_ABORTED);
	}

	if (pstrHostIFscanAttr->ch_freq_list != NULL) {
		kfree(pstrHostIFscanAttr->ch_freq_list);
		pstrHostIFscanAttr->ch_freq_list = NULL;
	}

	if (pstrHostIFscanAttr->ies != NULL) {
		kfree(pstrHostIFscanAttr->ies);
		pstrHostIFscanAttr->ies = NULL;
	}
	if (pstrHostIFscanAttr->hidden_network.pstrHiddenNetworkInfo != NULL)	{
		kfree(pstrHostIFscanAttr->hidden_network.pstrHiddenNetworkInfo);
		pstrHostIFscanAttr->hidden_network.pstrHiddenNetworkInfo = NULL;
	}

	if (pu8HdnNtwrksWidVal != NULL)
		kfree(pu8HdnNtwrksWidVal);

	return s32Error;
}

static s32 Handle_ScanDone(struct host_if_drv *hif_drv,
			   enum scan_event enuEvent)
{
	s32 s32Error = 0;
	u8 u8abort_running_scan;
	struct wid strWID;


	PRINT_D(HOSTINF_DBG, "in Handle_ScanDone()\n");

	if (enuEvent == SCAN_EVENT_ABORTED) {
		PRINT_D(GENERIC_DBG, "Abort running scan\n");
		u8abort_running_scan = 1;
		strWID.id = (u16)WID_ABORT_RUNNING_SCAN;
		strWID.type = WID_CHAR;
		strWID.val = (s8 *)&u8abort_running_scan;
		strWID.size = sizeof(char);

		s32Error = send_config_pkt(SET_CFG, &strWID, 1,
					   get_id_from_handler(hif_drv));
		if (s32Error) {
			PRINT_ER("Failed to set abort running scan\n");
			s32Error = -EFAULT;
		}
	}

	if (!hif_drv) {
		PRINT_ER("Driver handler is NULL\n");
		return s32Error;
	}

	if (hif_drv->strWILC_UsrScanReq.pfUserScanResult) {
		hif_drv->strWILC_UsrScanReq.pfUserScanResult(enuEvent, NULL,
								hif_drv->strWILC_UsrScanReq.u32UserScanPvoid, NULL);
		hif_drv->strWILC_UsrScanReq.pfUserScanResult = NULL;
	}

	return s32Error;
}

u8 u8ConnectedSSID[6] = {0};
static s32 Handle_Connect(struct host_if_drv *hif_drv,
			  struct connect_attr *pstrHostIFconnectAttr)
{
	s32 s32Error = 0;
	struct wid strWIDList[8];
	u32 u32WidsCount = 0, dummyval = 0;
	u8 *pu8CurrByte = NULL;
	struct join_bss_param *ptstrJoinBssParam;

	PRINT_D(GENERIC_DBG, "Handling connect request\n");

	if (memcmp(pstrHostIFconnectAttr->bssid, u8ConnectedSSID, ETH_ALEN) == 0) {

		s32Error = 0;
		PRINT_ER("Trying to connect to an already connected AP, Discard connect request\n");
		return s32Error;
	}

	PRINT_INFO(HOSTINF_DBG, "Saving connection parameters in global structure\n");

	ptstrJoinBssParam = (struct join_bss_param *)pstrHostIFconnectAttr->params;
	if (ptstrJoinBssParam == NULL) {
		PRINT_ER("Required BSSID not found\n");
		s32Error = -ENOENT;
		goto ERRORHANDLER;
	}

	if (pstrHostIFconnectAttr->bssid != NULL) {
		hif_drv->strWILC_UsrConnReq.pu8bssid = kmalloc(6, GFP_KERNEL);
		memcpy(hif_drv->strWILC_UsrConnReq.pu8bssid, pstrHostIFconnectAttr->bssid, 6);
	}

	hif_drv->strWILC_UsrConnReq.ssidLen = pstrHostIFconnectAttr->ssid_len;
	if (pstrHostIFconnectAttr->ssid != NULL) {
		hif_drv->strWILC_UsrConnReq.pu8ssid = kmalloc(pstrHostIFconnectAttr->ssid_len + 1, GFP_KERNEL);
		memcpy(hif_drv->strWILC_UsrConnReq.pu8ssid, pstrHostIFconnectAttr->ssid,
			    pstrHostIFconnectAttr->ssid_len);
		hif_drv->strWILC_UsrConnReq.pu8ssid[pstrHostIFconnectAttr->ssid_len] = '\0';
	}

	hif_drv->strWILC_UsrConnReq.ConnReqIEsLen = pstrHostIFconnectAttr->ies_len;
	if (pstrHostIFconnectAttr->ies != NULL) {
		hif_drv->strWILC_UsrConnReq.pu8ConnReqIEs = kmalloc(pstrHostIFconnectAttr->ies_len, GFP_KERNEL);
		memcpy(hif_drv->strWILC_UsrConnReq.pu8ConnReqIEs, pstrHostIFconnectAttr->ies,
			    pstrHostIFconnectAttr->ies_len);
	}

	hif_drv->strWILC_UsrConnReq.u8security = pstrHostIFconnectAttr->security;
	hif_drv->strWILC_UsrConnReq.tenuAuth_type = pstrHostIFconnectAttr->auth_type;
	hif_drv->strWILC_UsrConnReq.pfUserConnectResult = pstrHostIFconnectAttr->result;
	hif_drv->strWILC_UsrConnReq.u32UserConnectPvoid = pstrHostIFconnectAttr->arg;

	strWIDList[u32WidsCount].id = WID_SUCCESS_FRAME_COUNT;
	strWIDList[u32WidsCount].type = WID_INT;
	strWIDList[u32WidsCount].size = sizeof(u32);
	strWIDList[u32WidsCount].val = (s8 *)(&(dummyval));
	u32WidsCount++;

	strWIDList[u32WidsCount].id = WID_RECEIVED_FRAGMENT_COUNT;
	strWIDList[u32WidsCount].type = WID_INT;
	strWIDList[u32WidsCount].size = sizeof(u32);
	strWIDList[u32WidsCount].val = (s8 *)(&(dummyval));
	u32WidsCount++;

	strWIDList[u32WidsCount].id = WID_FAILED_COUNT;
	strWIDList[u32WidsCount].type = WID_INT;
	strWIDList[u32WidsCount].size = sizeof(u32);
	strWIDList[u32WidsCount].val = (s8 *)(&(dummyval));
	u32WidsCount++;

	{
		strWIDList[u32WidsCount].id = WID_INFO_ELEMENT_ASSOCIATE;
		strWIDList[u32WidsCount].type = WID_BIN_DATA;
		strWIDList[u32WidsCount].val = hif_drv->strWILC_UsrConnReq.pu8ConnReqIEs;
		strWIDList[u32WidsCount].size = hif_drv->strWILC_UsrConnReq.ConnReqIEsLen;
		u32WidsCount++;

		if (memcmp("DIRECT-", pstrHostIFconnectAttr->ssid, 7)) {

			gu32FlushedInfoElemAsocSize = hif_drv->strWILC_UsrConnReq.ConnReqIEsLen;
			gu8FlushedInfoElemAsoc =  kmalloc(gu32FlushedInfoElemAsocSize, GFP_KERNEL);
			memcpy(gu8FlushedInfoElemAsoc, hif_drv->strWILC_UsrConnReq.pu8ConnReqIEs,
			       gu32FlushedInfoElemAsocSize);
		}
	}
	strWIDList[u32WidsCount].id = (u16)WID_11I_MODE;
	strWIDList[u32WidsCount].type = WID_CHAR;
	strWIDList[u32WidsCount].size = sizeof(char);
	strWIDList[u32WidsCount].val = (s8 *)(&(hif_drv->strWILC_UsrConnReq.u8security));
	u32WidsCount++;

	if (memcmp("DIRECT-", pstrHostIFconnectAttr->ssid, 7))
		gu8Flushed11iMode = hif_drv->strWILC_UsrConnReq.u8security;

	PRINT_INFO(HOSTINF_DBG, "Encrypt Mode = %x\n", hif_drv->strWILC_UsrConnReq.u8security);


	strWIDList[u32WidsCount].id = (u16)WID_AUTH_TYPE;
	strWIDList[u32WidsCount].type = WID_CHAR;
	strWIDList[u32WidsCount].size = sizeof(char);
	strWIDList[u32WidsCount].val = (s8 *)(&hif_drv->strWILC_UsrConnReq.tenuAuth_type);
	u32WidsCount++;

	if (memcmp("DIRECT-", pstrHostIFconnectAttr->ssid, 7))
		gu8FlushedAuthType = (u8)hif_drv->strWILC_UsrConnReq.tenuAuth_type;

	PRINT_INFO(HOSTINF_DBG, "Authentication Type = %x\n", hif_drv->strWILC_UsrConnReq.tenuAuth_type);
	PRINT_D(HOSTINF_DBG, "Connecting to network of SSID %s on channel %d\n",
		hif_drv->strWILC_UsrConnReq.pu8ssid, pstrHostIFconnectAttr->ch);

	strWIDList[u32WidsCount].id = (u16)WID_JOIN_REQ_EXTENDED;
	strWIDList[u32WidsCount].type = WID_STR;
	strWIDList[u32WidsCount].size = 112;
	strWIDList[u32WidsCount].val = kmalloc(strWIDList[u32WidsCount].size, GFP_KERNEL);

	if (memcmp("DIRECT-", pstrHostIFconnectAttr->ssid, 7)) {
		gu32FlushedJoinReqSize = strWIDList[u32WidsCount].size;
		gu8FlushedJoinReq = kmalloc(gu32FlushedJoinReqSize, GFP_KERNEL);
	}
	if (strWIDList[u32WidsCount].val == NULL) {
		s32Error = -EFAULT;
		goto ERRORHANDLER;
	}

	pu8CurrByte = strWIDList[u32WidsCount].val;


	if (pstrHostIFconnectAttr->ssid != NULL) {
		memcpy(pu8CurrByte, pstrHostIFconnectAttr->ssid, pstrHostIFconnectAttr->ssid_len);
		pu8CurrByte[pstrHostIFconnectAttr->ssid_len] = '\0';
	}
	pu8CurrByte += MAX_SSID_LEN;
	*(pu8CurrByte++) = INFRASTRUCTURE;

	if ((pstrHostIFconnectAttr->ch >= 1) && (pstrHostIFconnectAttr->ch <= 14)) {
		*(pu8CurrByte++) = pstrHostIFconnectAttr->ch;
	} else {
		PRINT_ER("Channel out of range\n");
		*(pu8CurrByte++) = 0xFF;
	}
	*(pu8CurrByte++)  = (ptstrJoinBssParam->cap_info) & 0xFF;
	*(pu8CurrByte++)  = ((ptstrJoinBssParam->cap_info) >> 8) & 0xFF;
	PRINT_D(HOSTINF_DBG, "* Cap Info %0x*\n", (*(pu8CurrByte - 2) | ((*(pu8CurrByte - 1)) << 8)));

	if (pstrHostIFconnectAttr->bssid != NULL)
		memcpy(pu8CurrByte, pstrHostIFconnectAttr->bssid, 6);
	pu8CurrByte += 6;

	*(pu8CurrByte++)  = (ptstrJoinBssParam->beacon_period) & 0xFF;
	*(pu8CurrByte++)  = ((ptstrJoinBssParam->beacon_period) >> 8) & 0xFF;
	PRINT_D(HOSTINF_DBG, "* Beacon Period %d*\n", (*(pu8CurrByte - 2) | ((*(pu8CurrByte - 1)) << 8)));
	*(pu8CurrByte++)  =  ptstrJoinBssParam->dtim_period;
	PRINT_D(HOSTINF_DBG, "* DTIM Period %d*\n", (*(pu8CurrByte - 1)));

	memcpy(pu8CurrByte, ptstrJoinBssParam->supp_rates, MAX_RATES_SUPPORTED + 1);
	pu8CurrByte += (MAX_RATES_SUPPORTED + 1);

	*(pu8CurrByte++)  =  ptstrJoinBssParam->wmm_cap;
	PRINT_D(HOSTINF_DBG, "* wmm cap%d*\n", (*(pu8CurrByte - 1)));
	*(pu8CurrByte++)  = ptstrJoinBssParam->uapsd_cap;

	*(pu8CurrByte++)  = ptstrJoinBssParam->ht_capable;
	hif_drv->strWILC_UsrConnReq.IsHTCapable = ptstrJoinBssParam->ht_capable;

	*(pu8CurrByte++)  =  ptstrJoinBssParam->rsn_found;
	PRINT_D(HOSTINF_DBG, "* rsn found %d*\n", *(pu8CurrByte - 1));
	*(pu8CurrByte++)  =  ptstrJoinBssParam->rsn_grp_policy;
	PRINT_D(HOSTINF_DBG, "* rsn group policy %0x*\n", (*(pu8CurrByte - 1)));
	*(pu8CurrByte++) =  ptstrJoinBssParam->mode_802_11i;
	PRINT_D(HOSTINF_DBG, "* mode_802_11i %d*\n", (*(pu8CurrByte - 1)));

	memcpy(pu8CurrByte, ptstrJoinBssParam->rsn_pcip_policy, sizeof(ptstrJoinBssParam->rsn_pcip_policy));
	pu8CurrByte += sizeof(ptstrJoinBssParam->rsn_pcip_policy);

	memcpy(pu8CurrByte, ptstrJoinBssParam->rsn_auth_policy, sizeof(ptstrJoinBssParam->rsn_auth_policy));
	pu8CurrByte += sizeof(ptstrJoinBssParam->rsn_auth_policy);

	memcpy(pu8CurrByte, ptstrJoinBssParam->rsn_cap, sizeof(ptstrJoinBssParam->rsn_cap));
	pu8CurrByte += sizeof(ptstrJoinBssParam->rsn_cap);

	*(pu8CurrByte++) = REAL_JOIN_REQ;

	*(pu8CurrByte++) = ptstrJoinBssParam->u8NoaEnbaled;
	if (ptstrJoinBssParam->u8NoaEnbaled) {
		PRINT_D(HOSTINF_DBG, "NOA present\n");

		*(pu8CurrByte++) = (ptstrJoinBssParam->tsf) & 0xFF;
		*(pu8CurrByte++) = ((ptstrJoinBssParam->tsf) >> 8) & 0xFF;
		*(pu8CurrByte++) = ((ptstrJoinBssParam->tsf) >> 16) & 0xFF;
		*(pu8CurrByte++) = ((ptstrJoinBssParam->tsf) >> 24) & 0xFF;

		*(pu8CurrByte++) = ptstrJoinBssParam->u8Index;

		*(pu8CurrByte++) = ptstrJoinBssParam->u8OppEnable;

		if (ptstrJoinBssParam->u8OppEnable)
			*(pu8CurrByte++) = ptstrJoinBssParam->u8CtWindow;

		*(pu8CurrByte++) = ptstrJoinBssParam->u8Count;

		memcpy(pu8CurrByte, ptstrJoinBssParam->au8Duration, sizeof(ptstrJoinBssParam->au8Duration));

		pu8CurrByte += sizeof(ptstrJoinBssParam->au8Duration);

		memcpy(pu8CurrByte, ptstrJoinBssParam->au8Interval, sizeof(ptstrJoinBssParam->au8Interval));

		pu8CurrByte += sizeof(ptstrJoinBssParam->au8Interval);

		memcpy(pu8CurrByte, ptstrJoinBssParam->au8StartTime, sizeof(ptstrJoinBssParam->au8StartTime));

		pu8CurrByte += sizeof(ptstrJoinBssParam->au8StartTime);

	} else
		PRINT_D(HOSTINF_DBG, "NOA not present\n");

	pu8CurrByte = strWIDList[u32WidsCount].val;
	u32WidsCount++;
	gu32WidConnRstHack = 0;

	if (memcmp("DIRECT-", pstrHostIFconnectAttr->ssid, 7)) {
		memcpy(gu8FlushedJoinReq, pu8CurrByte, gu32FlushedJoinReqSize);
		gu8FlushedJoinReqDrvHandler = hif_drv;
	}

	PRINT_D(GENERIC_DBG, "send HOST_IF_WAITING_CONN_RESP\n");

	if (pstrHostIFconnectAttr->bssid != NULL) {
		memcpy(u8ConnectedSSID, pstrHostIFconnectAttr->bssid, ETH_ALEN);

		PRINT_D(GENERIC_DBG, "save Bssid = %pM\n", pstrHostIFconnectAttr->bssid);
		PRINT_D(GENERIC_DBG, "save bssid = %pM\n", u8ConnectedSSID);
	}

	s32Error = send_config_pkt(SET_CFG, strWIDList, u32WidsCount,
				   get_id_from_handler(hif_drv));
	if (s32Error) {
		PRINT_ER("failed to send config packet\n");
		s32Error = -EFAULT;
		goto ERRORHANDLER;
	} else {
		PRINT_D(GENERIC_DBG, "set HOST_IF_WAITING_CONN_RESP\n");
		hif_drv->enuHostIFstate = HOST_IF_WAITING_CONN_RESP;
	}

ERRORHANDLER:
	if (s32Error) {
		tstrConnectInfo strConnectInfo;

		del_timer(&hif_drv->hConnectTimer);

		PRINT_D(HOSTINF_DBG, "could not start connecting to the required network\n");

		memset(&strConnectInfo, 0, sizeof(tstrConnectInfo));

		if (pstrHostIFconnectAttr->result != NULL) {
			if (pstrHostIFconnectAttr->bssid != NULL)
				memcpy(strConnectInfo.au8bssid, pstrHostIFconnectAttr->bssid, 6);

			if (pstrHostIFconnectAttr->ies != NULL) {
				strConnectInfo.ReqIEsLen = pstrHostIFconnectAttr->ies_len;
				strConnectInfo.pu8ReqIEs = kmalloc(pstrHostIFconnectAttr->ies_len, GFP_KERNEL);
				memcpy(strConnectInfo.pu8ReqIEs,
					    pstrHostIFconnectAttr->ies,
					    pstrHostIFconnectAttr->ies_len);
			}

			pstrHostIFconnectAttr->result(CONN_DISCONN_EVENT_CONN_RESP,
							       &strConnectInfo,
							       MAC_DISCONNECTED,
							       NULL,
							       pstrHostIFconnectAttr->arg);
			hif_drv->enuHostIFstate = HOST_IF_IDLE;
			if (strConnectInfo.pu8ReqIEs != NULL) {
				kfree(strConnectInfo.pu8ReqIEs);
				strConnectInfo.pu8ReqIEs = NULL;
			}

		} else {
			PRINT_ER("Connect callback function pointer is NULL\n");
		}
	}

	PRINT_D(HOSTINF_DBG, "Deallocating connection parameters\n");
	if (pstrHostIFconnectAttr->bssid != NULL) {
		kfree(pstrHostIFconnectAttr->bssid);
		pstrHostIFconnectAttr->bssid = NULL;
	}

	if (pstrHostIFconnectAttr->ssid != NULL) {
		kfree(pstrHostIFconnectAttr->ssid);
		pstrHostIFconnectAttr->ssid = NULL;
	}

	if (pstrHostIFconnectAttr->ies != NULL) {
		kfree(pstrHostIFconnectAttr->ies);
		pstrHostIFconnectAttr->ies = NULL;
	}

	if (pu8CurrByte != NULL)
		kfree(pu8CurrByte);
	return s32Error;
}

static s32 Handle_FlushConnect(struct host_if_drv *hif_drv)
{
	s32 s32Error = 0;
	struct wid strWIDList[5];
	u32 u32WidsCount = 0;
	u8 *pu8CurrByte = NULL;

	strWIDList[u32WidsCount].id = WID_INFO_ELEMENT_ASSOCIATE;
	strWIDList[u32WidsCount].type = WID_BIN_DATA;
	strWIDList[u32WidsCount].val = gu8FlushedInfoElemAsoc;
	strWIDList[u32WidsCount].size = gu32FlushedInfoElemAsocSize;
	u32WidsCount++;

	strWIDList[u32WidsCount].id = (u16)WID_11I_MODE;
	strWIDList[u32WidsCount].type = WID_CHAR;
	strWIDList[u32WidsCount].size = sizeof(char);
	strWIDList[u32WidsCount].val = (s8 *)(&(gu8Flushed11iMode));
	u32WidsCount++;



	strWIDList[u32WidsCount].id = (u16)WID_AUTH_TYPE;
	strWIDList[u32WidsCount].type = WID_CHAR;
	strWIDList[u32WidsCount].size = sizeof(char);
	strWIDList[u32WidsCount].val = (s8 *)(&gu8FlushedAuthType);
	u32WidsCount++;

	strWIDList[u32WidsCount].id = (u16)WID_JOIN_REQ_EXTENDED;
	strWIDList[u32WidsCount].type = WID_STR;
	strWIDList[u32WidsCount].size = gu32FlushedJoinReqSize;
	strWIDList[u32WidsCount].val = (s8 *)gu8FlushedJoinReq;
	pu8CurrByte = strWIDList[u32WidsCount].val;

	pu8CurrByte += FLUSHED_BYTE_POS;
	*(pu8CurrByte) = FLUSHED_JOIN_REQ;

	u32WidsCount++;

	s32Error = send_config_pkt(SET_CFG, strWIDList, u32WidsCount,
				   get_id_from_handler(gu8FlushedJoinReqDrvHandler));
	if (s32Error) {
		PRINT_ER("failed to send config packet\n");
		s32Error = -EINVAL;
	}

	return s32Error;
}

static s32 Handle_ConnectTimeout(struct host_if_drv *hif_drv)
{
	s32 s32Error = 0;
	tstrConnectInfo strConnectInfo;
	struct wid strWID;
	u16 u16DummyReasonCode = 0;

	if (!hif_drv) {
		PRINT_ER("Driver handler is NULL\n");
		return s32Error;
	}

	hif_drv->enuHostIFstate = HOST_IF_IDLE;

	gbScanWhileConnected = false;


	memset(&strConnectInfo, 0, sizeof(tstrConnectInfo));

	if (hif_drv->strWILC_UsrConnReq.pfUserConnectResult != NULL)	{
		if (hif_drv->strWILC_UsrConnReq.pu8bssid != NULL) {
			memcpy(strConnectInfo.au8bssid,
				    hif_drv->strWILC_UsrConnReq.pu8bssid, 6);
		}

		if (hif_drv->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) {
			strConnectInfo.ReqIEsLen = hif_drv->strWILC_UsrConnReq.ConnReqIEsLen;
			strConnectInfo.pu8ReqIEs = kmalloc(hif_drv->strWILC_UsrConnReq.ConnReqIEsLen, GFP_KERNEL);
			memcpy(strConnectInfo.pu8ReqIEs,
				    hif_drv->strWILC_UsrConnReq.pu8ConnReqIEs,
				    hif_drv->strWILC_UsrConnReq.ConnReqIEsLen);
		}

		hif_drv->strWILC_UsrConnReq.pfUserConnectResult(CONN_DISCONN_EVENT_CONN_RESP,
								   &strConnectInfo,
								   MAC_DISCONNECTED,
								   NULL,
								   hif_drv->strWILC_UsrConnReq.u32UserConnectPvoid);

		if (strConnectInfo.pu8ReqIEs != NULL) {
			kfree(strConnectInfo.pu8ReqIEs);
			strConnectInfo.pu8ReqIEs = NULL;
		}
	} else {
		PRINT_ER("Connect callback function pointer is NULL\n");
	}

	strWID.id = (u16)WID_DISCONNECT;
	strWID.type = WID_CHAR;
	strWID.val = (s8 *)&u16DummyReasonCode;
	strWID.size = sizeof(char);

	PRINT_D(HOSTINF_DBG, "Sending disconnect request\n");

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error)
		PRINT_ER("Failed to send dissconect config packet\n");

	hif_drv->strWILC_UsrConnReq.ssidLen = 0;
	kfree(hif_drv->strWILC_UsrConnReq.pu8ssid);
	kfree(hif_drv->strWILC_UsrConnReq.pu8bssid);
	hif_drv->strWILC_UsrConnReq.ConnReqIEsLen = 0;
	kfree(hif_drv->strWILC_UsrConnReq.pu8ConnReqIEs);

	eth_zero_addr(u8ConnectedSSID);

	if (gu8FlushedJoinReq != NULL && gu8FlushedJoinReqDrvHandler == hif_drv) {
		kfree(gu8FlushedJoinReq);
		gu8FlushedJoinReq = NULL;
	}
	if (gu8FlushedInfoElemAsoc != NULL && gu8FlushedJoinReqDrvHandler == hif_drv) {
		kfree(gu8FlushedInfoElemAsoc);
		gu8FlushedInfoElemAsoc = NULL;
	}

	return s32Error;
}

static s32 Handle_RcvdNtwrkInfo(struct host_if_drv *hif_drv,
				struct rcvd_net_info *pstrRcvdNetworkInfo)
{
	u32 i;
	bool bNewNtwrkFound;



	s32 s32Error = 0;
	tstrNetworkInfo *pstrNetworkInfo = NULL;
	void *pJoinParams = NULL;

	bNewNtwrkFound = true;
	PRINT_INFO(HOSTINF_DBG, "Handling received network info\n");

	if (hif_drv->strWILC_UsrScanReq.pfUserScanResult) {
		PRINT_D(HOSTINF_DBG, "State: Scanning, parsing network information received\n");
		parse_network_info(pstrRcvdNetworkInfo->pu8Buffer, &pstrNetworkInfo);
		if ((pstrNetworkInfo == NULL)
		    || (hif_drv->strWILC_UsrScanReq.pfUserScanResult == NULL)) {
			PRINT_ER("driver is null\n");
			s32Error = -EINVAL;
			goto done;
		}

		for (i = 0; i < hif_drv->strWILC_UsrScanReq.u32RcvdChCount; i++) {

			if ((hif_drv->strWILC_UsrScanReq.astrFoundNetworkInfo[i].au8bssid != NULL) &&
			    (pstrNetworkInfo->au8bssid != NULL)) {
				if (memcmp(hif_drv->strWILC_UsrScanReq.astrFoundNetworkInfo[i].au8bssid,
						pstrNetworkInfo->au8bssid, 6) == 0) {
					if (pstrNetworkInfo->s8rssi <= hif_drv->strWILC_UsrScanReq.astrFoundNetworkInfo[i].s8rssi) {
						PRINT_D(HOSTINF_DBG, "Network previously discovered\n");
						goto done;
					} else {
						hif_drv->strWILC_UsrScanReq.astrFoundNetworkInfo[i].s8rssi = pstrNetworkInfo->s8rssi;
						bNewNtwrkFound = false;
						break;
					}
				}
			}
		}

		if (bNewNtwrkFound == true) {
			PRINT_D(HOSTINF_DBG, "New network found\n");

			if (hif_drv->strWILC_UsrScanReq.u32RcvdChCount < MAX_NUM_SCANNED_NETWORKS) {
				hif_drv->strWILC_UsrScanReq.astrFoundNetworkInfo[hif_drv->strWILC_UsrScanReq.u32RcvdChCount].s8rssi = pstrNetworkInfo->s8rssi;

				if ((hif_drv->strWILC_UsrScanReq.astrFoundNetworkInfo[hif_drv->strWILC_UsrScanReq.u32RcvdChCount].au8bssid != NULL)
				    && (pstrNetworkInfo->au8bssid != NULL)) {
					memcpy(hif_drv->strWILC_UsrScanReq.astrFoundNetworkInfo[hif_drv->strWILC_UsrScanReq.u32RcvdChCount].au8bssid,
						    pstrNetworkInfo->au8bssid, 6);

					hif_drv->strWILC_UsrScanReq.u32RcvdChCount++;

					pstrNetworkInfo->bNewNetwork = true;
					pJoinParams = host_int_ParseJoinBssParam(pstrNetworkInfo);

					hif_drv->strWILC_UsrScanReq.pfUserScanResult(SCAN_EVENT_NETWORK_FOUND, pstrNetworkInfo,
											hif_drv->strWILC_UsrScanReq.u32UserScanPvoid,
											pJoinParams);


				}
			} else {
				PRINT_WRN(HOSTINF_DBG, "Discovered networks exceeded max. limit\n");
			}
		} else {
			pstrNetworkInfo->bNewNetwork = false;
			hif_drv->strWILC_UsrScanReq.pfUserScanResult(SCAN_EVENT_NETWORK_FOUND, pstrNetworkInfo,
									hif_drv->strWILC_UsrScanReq.u32UserScanPvoid, NULL);
		}
	}

done:
	if (pstrRcvdNetworkInfo->pu8Buffer != NULL) {
		kfree(pstrRcvdNetworkInfo->pu8Buffer);
		pstrRcvdNetworkInfo->pu8Buffer = NULL;
	}

	if (pstrNetworkInfo != NULL) {
		DeallocateNetworkInfo(pstrNetworkInfo);
		pstrNetworkInfo = NULL;
	}

	return s32Error;
}

static s32 Handle_RcvdGnrlAsyncInfo(struct host_if_drv *hif_drv,
				    struct rcvd_async_info *pstrRcvdGnrlAsyncInfo)
{
	s32 s32Error = 0;
	u8 u8MsgType = 0;
	u8 u8MsgID = 0;
	u16 u16MsgLen = 0;
	u16 u16WidID = (u16)WID_NIL;
	u8 u8WidLen  = 0;
	u8 u8MacStatus;
	u8 u8MacStatusReasonCode;
	u8 u8MacStatusAdditionalInfo;
	tstrConnectInfo strConnectInfo;
	tstrDisconnectNotifInfo strDisconnectNotifInfo;
	s32 s32Err = 0;

	if (!hif_drv) {
		PRINT_ER("Driver handler is NULL\n");
		return -ENODEV;
	}
	PRINT_D(GENERIC_DBG, "Current State = %d,Received state = %d\n", hif_drv->enuHostIFstate,
		pstrRcvdGnrlAsyncInfo->buffer[7]);

	if ((hif_drv->enuHostIFstate == HOST_IF_WAITING_CONN_RESP) ||
	    (hif_drv->enuHostIFstate == HOST_IF_CONNECTED) ||
	    hif_drv->strWILC_UsrScanReq.pfUserScanResult) {
		if ((pstrRcvdGnrlAsyncInfo->buffer == NULL) ||
		    (hif_drv->strWILC_UsrConnReq.pfUserConnectResult == NULL)) {
			PRINT_ER("driver is null\n");
			return -EINVAL;
		}

		u8MsgType = pstrRcvdGnrlAsyncInfo->buffer[0];

		if ('I' != u8MsgType) {
			PRINT_ER("Received Message format incorrect.\n");
			return -EFAULT;
		}

		u8MsgID = pstrRcvdGnrlAsyncInfo->buffer[1];
		u16MsgLen = MAKE_WORD16(pstrRcvdGnrlAsyncInfo->buffer[2], pstrRcvdGnrlAsyncInfo->buffer[3]);
		u16WidID = MAKE_WORD16(pstrRcvdGnrlAsyncInfo->buffer[4], pstrRcvdGnrlAsyncInfo->buffer[5]);
		u8WidLen = pstrRcvdGnrlAsyncInfo->buffer[6];
		u8MacStatus  = pstrRcvdGnrlAsyncInfo->buffer[7];
		u8MacStatusReasonCode = pstrRcvdGnrlAsyncInfo->buffer[8];
		u8MacStatusAdditionalInfo = pstrRcvdGnrlAsyncInfo->buffer[9];
		PRINT_INFO(HOSTINF_DBG, "Recieved MAC status = %d with Reason = %d , Info = %d\n", u8MacStatus, u8MacStatusReasonCode, u8MacStatusAdditionalInfo);
		if (hif_drv->enuHostIFstate == HOST_IF_WAITING_CONN_RESP) {
			u32 u32RcvdAssocRespInfoLen;
			tstrConnectRespInfo *pstrConnectRespInfo = NULL;

			PRINT_D(HOSTINF_DBG, "Recieved MAC status = %d with Reason = %d , Code = %d\n", u8MacStatus, u8MacStatusReasonCode, u8MacStatusAdditionalInfo);

			memset(&strConnectInfo, 0, sizeof(tstrConnectInfo));

			if (u8MacStatus == MAC_CONNECTED) {
				memset(gapu8RcvdAssocResp, 0, MAX_ASSOC_RESP_FRAME_SIZE);

				host_int_get_assoc_res_info(hif_drv,
							    gapu8RcvdAssocResp,
							    MAX_ASSOC_RESP_FRAME_SIZE,
							    &u32RcvdAssocRespInfoLen);

				PRINT_INFO(HOSTINF_DBG, "Received association response with length = %d\n", u32RcvdAssocRespInfoLen);

				if (u32RcvdAssocRespInfoLen != 0) {

					PRINT_D(HOSTINF_DBG, "Parsing association response\n");
					s32Err = ParseAssocRespInfo(gapu8RcvdAssocResp, u32RcvdAssocRespInfoLen,
								    &pstrConnectRespInfo);
					if (s32Err) {
						PRINT_ER("ParseAssocRespInfo() returned error %d\n", s32Err);
					} else {
						strConnectInfo.u16ConnectStatus = pstrConnectRespInfo->u16ConnectStatus;

						if (strConnectInfo.u16ConnectStatus == SUCCESSFUL_STATUSCODE) {
							PRINT_INFO(HOSTINF_DBG, "Association response received : Successful connection status\n");
							if (pstrConnectRespInfo->pu8RespIEs != NULL) {
								strConnectInfo.u16RespIEsLen = pstrConnectRespInfo->u16RespIEsLen;


								strConnectInfo.pu8RespIEs = kmalloc(pstrConnectRespInfo->u16RespIEsLen, GFP_KERNEL);
								memcpy(strConnectInfo.pu8RespIEs, pstrConnectRespInfo->pu8RespIEs,
									    pstrConnectRespInfo->u16RespIEsLen);
							}
						}

						if (pstrConnectRespInfo != NULL) {
							DeallocateAssocRespInfo(pstrConnectRespInfo);
							pstrConnectRespInfo = NULL;
						}
					}
				}
			}

			if ((u8MacStatus == MAC_CONNECTED) &&
			    (strConnectInfo.u16ConnectStatus != SUCCESSFUL_STATUSCODE))	{
				PRINT_ER("Received MAC status is MAC_CONNECTED while the received status code in Asoc Resp is not SUCCESSFUL_STATUSCODE\n");
				eth_zero_addr(u8ConnectedSSID);

			} else if (u8MacStatus == MAC_DISCONNECTED)    {
				PRINT_ER("Received MAC status is MAC_DISCONNECTED\n");
				eth_zero_addr(u8ConnectedSSID);
			}

			if (hif_drv->strWILC_UsrConnReq.pu8bssid != NULL) {
				PRINT_D(HOSTINF_DBG, "Retrieving actual BSSID from AP\n");
				memcpy(strConnectInfo.au8bssid, hif_drv->strWILC_UsrConnReq.pu8bssid, 6);

				if ((u8MacStatus == MAC_CONNECTED) &&
				    (strConnectInfo.u16ConnectStatus == SUCCESSFUL_STATUSCODE))	{
					memcpy(hif_drv->au8AssociatedBSSID,
						    hif_drv->strWILC_UsrConnReq.pu8bssid, ETH_ALEN);
				}
			}


			if (hif_drv->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) {
				strConnectInfo.ReqIEsLen = hif_drv->strWILC_UsrConnReq.ConnReqIEsLen;
				strConnectInfo.pu8ReqIEs = kmalloc(hif_drv->strWILC_UsrConnReq.ConnReqIEsLen, GFP_KERNEL);
				memcpy(strConnectInfo.pu8ReqIEs,
					    hif_drv->strWILC_UsrConnReq.pu8ConnReqIEs,
					    hif_drv->strWILC_UsrConnReq.ConnReqIEsLen);
			}


			del_timer(&hif_drv->hConnectTimer);
			hif_drv->strWILC_UsrConnReq.pfUserConnectResult(CONN_DISCONN_EVENT_CONN_RESP,
									   &strConnectInfo,
									   u8MacStatus,
									   NULL,
									   hif_drv->strWILC_UsrConnReq.u32UserConnectPvoid);

			if ((u8MacStatus == MAC_CONNECTED) &&
			    (strConnectInfo.u16ConnectStatus == SUCCESSFUL_STATUSCODE))	{
				host_int_set_power_mgmt(hif_drv, 0, 0);

				PRINT_D(HOSTINF_DBG, "MAC status : CONNECTED and Connect Status : Successful\n");
				hif_drv->enuHostIFstate = HOST_IF_CONNECTED;

				PRINT_D(GENERIC_DBG, "Obtaining an IP, Disable Scan\n");
				g_obtainingIP = true;
				mod_timer(&hDuringIpTimer,
					  jiffies + msecs_to_jiffies(10000));
			} else {
				PRINT_D(HOSTINF_DBG, "MAC status : %d and Connect Status : %d\n", u8MacStatus, strConnectInfo.u16ConnectStatus);
				hif_drv->enuHostIFstate = HOST_IF_IDLE;
				gbScanWhileConnected = false;
			}

			if (strConnectInfo.pu8RespIEs != NULL) {
				kfree(strConnectInfo.pu8RespIEs);
				strConnectInfo.pu8RespIEs = NULL;
			}

			if (strConnectInfo.pu8ReqIEs != NULL) {
				kfree(strConnectInfo.pu8ReqIEs);
				strConnectInfo.pu8ReqIEs = NULL;
			}
			hif_drv->strWILC_UsrConnReq.ssidLen = 0;
			kfree(hif_drv->strWILC_UsrConnReq.pu8ssid);
			kfree(hif_drv->strWILC_UsrConnReq.pu8bssid);
			hif_drv->strWILC_UsrConnReq.ConnReqIEsLen = 0;
			kfree(hif_drv->strWILC_UsrConnReq.pu8ConnReqIEs);
		} else if ((u8MacStatus == MAC_DISCONNECTED) &&
			   (hif_drv->enuHostIFstate == HOST_IF_CONNECTED)) {
			PRINT_D(HOSTINF_DBG, "Received MAC_DISCONNECTED from the FW\n");

			memset(&strDisconnectNotifInfo, 0, sizeof(tstrDisconnectNotifInfo));

			if (hif_drv->strWILC_UsrScanReq.pfUserScanResult) {
				PRINT_D(HOSTINF_DBG, "\n\n<< Abort the running OBSS Scan >>\n\n");
				del_timer(&hif_drv->hScanTimer);
				Handle_ScanDone((void *)hif_drv, SCAN_EVENT_ABORTED);
			}

			strDisconnectNotifInfo.u16reason = 0;
			strDisconnectNotifInfo.ie = NULL;
			strDisconnectNotifInfo.ie_len = 0;

			if (hif_drv->strWILC_UsrConnReq.pfUserConnectResult != NULL)	{
				g_obtainingIP = false;
				host_int_set_power_mgmt(hif_drv, 0, 0);

				hif_drv->strWILC_UsrConnReq.pfUserConnectResult(CONN_DISCONN_EVENT_DISCONN_NOTIF,
										   NULL,
										   0,
										   &strDisconnectNotifInfo,
										   hif_drv->strWILC_UsrConnReq.u32UserConnectPvoid);

			} else {
				PRINT_ER("Connect result callback function is NULL\n");
			}

			eth_zero_addr(hif_drv->au8AssociatedBSSID);

			hif_drv->strWILC_UsrConnReq.ssidLen = 0;
			kfree(hif_drv->strWILC_UsrConnReq.pu8ssid);
			kfree(hif_drv->strWILC_UsrConnReq.pu8bssid);
			hif_drv->strWILC_UsrConnReq.ConnReqIEsLen = 0;
			kfree(hif_drv->strWILC_UsrConnReq.pu8ConnReqIEs);

			if (gu8FlushedJoinReq != NULL && gu8FlushedJoinReqDrvHandler == hif_drv) {
				kfree(gu8FlushedJoinReq);
				gu8FlushedJoinReq = NULL;
			}
			if (gu8FlushedInfoElemAsoc != NULL && gu8FlushedJoinReqDrvHandler == hif_drv) {
				kfree(gu8FlushedInfoElemAsoc);
				gu8FlushedInfoElemAsoc = NULL;
			}

			hif_drv->enuHostIFstate = HOST_IF_IDLE;
			gbScanWhileConnected = false;

		} else if ((u8MacStatus == MAC_DISCONNECTED) &&
			   (hif_drv->strWILC_UsrScanReq.pfUserScanResult != NULL)) {
			PRINT_D(HOSTINF_DBG, "Received MAC_DISCONNECTED from the FW while scanning\n");
			PRINT_D(HOSTINF_DBG, "\n\n<< Abort the running Scan >>\n\n");

			del_timer(&hif_drv->hScanTimer);
			if (hif_drv->strWILC_UsrScanReq.pfUserScanResult)
				Handle_ScanDone(hif_drv, SCAN_EVENT_ABORTED);

		}

	}

	if (pstrRcvdGnrlAsyncInfo->buffer != NULL) {
		kfree(pstrRcvdGnrlAsyncInfo->buffer);
		pstrRcvdGnrlAsyncInfo->buffer = NULL;
	}

	return s32Error;
}

static int Handle_Key(struct host_if_drv *hif_drv,
		      struct key_attr *pstrHostIFkeyAttr)
{
	s32 s32Error = 0;
	struct wid strWID;
	struct wid strWIDList[5];
	u8 i;
	u8 *pu8keybuf;
	s8 s8idxarray[1];
	s8 ret = 0;

	switch (pstrHostIFkeyAttr->type) {


	case WEP:

		if (pstrHostIFkeyAttr->action & ADDKEY_AP) {

			PRINT_D(HOSTINF_DBG, "Handling WEP key\n");
			PRINT_D(GENERIC_DBG, "ID Hostint is %d\n", (pstrHostIFkeyAttr->attr.wep.index));
			strWIDList[0].id = (u16)WID_11I_MODE;
			strWIDList[0].type = WID_CHAR;
			strWIDList[0].size = sizeof(char);
			strWIDList[0].val = (s8 *)(&(pstrHostIFkeyAttr->attr.wep.mode));

			strWIDList[1].id = WID_AUTH_TYPE;
			strWIDList[1].type = WID_CHAR;
			strWIDList[1].size = sizeof(char);
			strWIDList[1].val = (s8 *)(&(pstrHostIFkeyAttr->attr.wep.auth_type));

			strWIDList[2].id = (u16)WID_KEY_ID;
			strWIDList[2].type = WID_CHAR;

			strWIDList[2].val = (s8 *)(&(pstrHostIFkeyAttr->attr.wep.index));
			strWIDList[2].size = sizeof(char);

			pu8keybuf = kmalloc(pstrHostIFkeyAttr->attr.wep.key_len, GFP_KERNEL);

			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send Key\n");
				return -1;
			}

			memcpy(pu8keybuf, pstrHostIFkeyAttr->attr.wep.key,
				    pstrHostIFkeyAttr->attr.wep.key_len);

			kfree(pstrHostIFkeyAttr->attr.wep.key);

			strWIDList[3].id = (u16)WID_WEP_KEY_VALUE;
			strWIDList[3].type = WID_STR;
			strWIDList[3].size = pstrHostIFkeyAttr->attr.wep.key_len;
			strWIDList[3].val = (s8 *)pu8keybuf;


			s32Error = send_config_pkt(SET_CFG, strWIDList, 4,
						   get_id_from_handler(hif_drv));
			kfree(pu8keybuf);


		}

		if (pstrHostIFkeyAttr->action & ADDKEY) {
			PRINT_D(HOSTINF_DBG, "Handling WEP key\n");
			pu8keybuf = kmalloc(pstrHostIFkeyAttr->attr.wep.key_len + 2, GFP_KERNEL);
			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send Key\n");
				return -1;
			}
			pu8keybuf[0] = pstrHostIFkeyAttr->attr.wep.index;
			memcpy(pu8keybuf + 1, &pstrHostIFkeyAttr->attr.wep.key_len, 1);
			memcpy(pu8keybuf + 2, pstrHostIFkeyAttr->attr.wep.key,
				    pstrHostIFkeyAttr->attr.wep.key_len);
			kfree(pstrHostIFkeyAttr->attr.wep.key);

			strWID.id = (u16)WID_ADD_WEP_KEY;
			strWID.type = WID_STR;
			strWID.val = (s8 *)pu8keybuf;
			strWID.size = pstrHostIFkeyAttr->attr.wep.key_len + 2;

			s32Error = send_config_pkt(SET_CFG, &strWID, 1,
						   get_id_from_handler(hif_drv));
			kfree(pu8keybuf);
		} else if (pstrHostIFkeyAttr->action & REMOVEKEY) {

			PRINT_D(HOSTINF_DBG, "Removing key\n");
			strWID.id = (u16)WID_REMOVE_WEP_KEY;
			strWID.type = WID_STR;

			s8idxarray[0] = (s8)pstrHostIFkeyAttr->attr.wep.index;
			strWID.val = s8idxarray;
			strWID.size = 1;

			s32Error = send_config_pkt(SET_CFG, &strWID, 1,
						   get_id_from_handler(hif_drv));
		} else {
			strWID.id = (u16)WID_KEY_ID;
			strWID.type = WID_CHAR;
			strWID.val = (s8 *)(&(pstrHostIFkeyAttr->attr.wep.index));
			strWID.size = sizeof(char);

			PRINT_D(HOSTINF_DBG, "Setting default key index\n");

			s32Error = send_config_pkt(SET_CFG, &strWID, 1,
						   get_id_from_handler(hif_drv));
		}
		up(&hif_drv->hSemTestKeyBlock);
		break;

	case WPARxGtk:
		if (pstrHostIFkeyAttr->action & ADDKEY_AP) {
			pu8keybuf = kmalloc(RX_MIC_KEY_MSG_LEN, GFP_KERNEL);
			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send RxGTK Key\n");
				ret = -1;
				goto _WPARxGtk_end_case_;
			}

			memset(pu8keybuf, 0, RX_MIC_KEY_MSG_LEN);

			if (pstrHostIFkeyAttr->attr.wpa.seq != NULL)
				memcpy(pu8keybuf + 6, pstrHostIFkeyAttr->attr.wpa.seq, 8);

			memcpy(pu8keybuf + 14, &pstrHostIFkeyAttr->attr.wpa.index, 1);
			memcpy(pu8keybuf + 15, &pstrHostIFkeyAttr->attr.wpa.key_len, 1);
			memcpy(pu8keybuf + 16, pstrHostIFkeyAttr->attr.wpa.key,
				    pstrHostIFkeyAttr->attr.wpa.key_len);

			strWIDList[0].id = (u16)WID_11I_MODE;
			strWIDList[0].type = WID_CHAR;
			strWIDList[0].size = sizeof(char);
			strWIDList[0].val = (s8 *)(&(pstrHostIFkeyAttr->attr.wpa.mode));

			strWIDList[1].id = (u16)WID_ADD_RX_GTK;
			strWIDList[1].type = WID_STR;
			strWIDList[1].val = (s8 *)pu8keybuf;
			strWIDList[1].size = RX_MIC_KEY_MSG_LEN;

			s32Error = send_config_pkt(SET_CFG, strWIDList, 2,
						   get_id_from_handler(hif_drv));

			kfree(pu8keybuf);
			up(&hif_drv->hSemTestKeyBlock);
		}

		if (pstrHostIFkeyAttr->action & ADDKEY) {
			PRINT_D(HOSTINF_DBG, "Handling group key(Rx) function\n");

			pu8keybuf = kmalloc(RX_MIC_KEY_MSG_LEN, GFP_KERNEL);
			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send RxGTK Key\n");
				ret = -1;
				goto _WPARxGtk_end_case_;
			}

			memset(pu8keybuf, 0, RX_MIC_KEY_MSG_LEN);

			if (hif_drv->enuHostIFstate == HOST_IF_CONNECTED)
				memcpy(pu8keybuf, hif_drv->au8AssociatedBSSID, ETH_ALEN);
			else
				PRINT_ER("Couldn't handle WPARxGtk while enuHostIFstate is not HOST_IF_CONNECTED\n");

			memcpy(pu8keybuf + 6, pstrHostIFkeyAttr->attr.wpa.seq, 8);
			memcpy(pu8keybuf + 14, &pstrHostIFkeyAttr->attr.wpa.index, 1);
			memcpy(pu8keybuf + 15, &pstrHostIFkeyAttr->attr.wpa.key_len, 1);
			memcpy(pu8keybuf + 16, pstrHostIFkeyAttr->attr.wpa.key,
				    pstrHostIFkeyAttr->attr.wpa.key_len);

			strWID.id = (u16)WID_ADD_RX_GTK;
			strWID.type = WID_STR;
			strWID.val = (s8 *)pu8keybuf;
			strWID.size = RX_MIC_KEY_MSG_LEN;

			s32Error = send_config_pkt(SET_CFG, &strWID, 1,
						   get_id_from_handler(hif_drv));

			kfree(pu8keybuf);
			up(&hif_drv->hSemTestKeyBlock);
		}
_WPARxGtk_end_case_:
		kfree(pstrHostIFkeyAttr->attr.wpa.key);
		kfree(pstrHostIFkeyAttr->attr.wpa.seq);
		if (ret == -1)
			return ret;

		break;

	case WPAPtk:
		if (pstrHostIFkeyAttr->action & ADDKEY_AP) {


			pu8keybuf = kmalloc(PTK_KEY_MSG_LEN + 1, GFP_KERNEL);



			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send PTK Key\n");
				ret = -1;
				goto _WPAPtk_end_case_;

			}

			memcpy(pu8keybuf, pstrHostIFkeyAttr->attr.wpa.mac_addr, 6);
			memcpy(pu8keybuf + 6, &pstrHostIFkeyAttr->attr.wpa.index, 1);
			memcpy(pu8keybuf + 7, &pstrHostIFkeyAttr->attr.wpa.key_len, 1);
			memcpy(pu8keybuf + 8, pstrHostIFkeyAttr->attr.wpa.key,
				    pstrHostIFkeyAttr->attr.wpa.key_len);

			strWIDList[0].id = (u16)WID_11I_MODE;
			strWIDList[0].type = WID_CHAR;
			strWIDList[0].size = sizeof(char);
			strWIDList[0].val = (s8 *)(&(pstrHostIFkeyAttr->attr.wpa.mode));

			strWIDList[1].id = (u16)WID_ADD_PTK;
			strWIDList[1].type = WID_STR;
			strWIDList[1].val = (s8 *)pu8keybuf;
			strWIDList[1].size = PTK_KEY_MSG_LEN + 1;

			s32Error = send_config_pkt(SET_CFG, strWIDList, 2,
						   get_id_from_handler(hif_drv));
			kfree(pu8keybuf);
			up(&hif_drv->hSemTestKeyBlock);
		}
		if (pstrHostIFkeyAttr->action & ADDKEY) {


			pu8keybuf = kmalloc(PTK_KEY_MSG_LEN, GFP_KERNEL);



			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send PTK Key\n");
				ret = -1;
				goto _WPAPtk_end_case_;

			}

			memcpy(pu8keybuf, pstrHostIFkeyAttr->attr.wpa.mac_addr, 6);
			memcpy(pu8keybuf + 6, &pstrHostIFkeyAttr->attr.wpa.key_len, 1);
			memcpy(pu8keybuf + 7, pstrHostIFkeyAttr->attr.wpa.key,
				    pstrHostIFkeyAttr->attr.wpa.key_len);

			strWID.id = (u16)WID_ADD_PTK;
			strWID.type = WID_STR;
			strWID.val = (s8 *)pu8keybuf;
			strWID.size = PTK_KEY_MSG_LEN;

			s32Error = send_config_pkt(SET_CFG, &strWID, 1,
						   get_id_from_handler(hif_drv));
			kfree(pu8keybuf);
			up(&hif_drv->hSemTestKeyBlock);
		}

_WPAPtk_end_case_:
		kfree(pstrHostIFkeyAttr->attr.wpa.key);
		if (ret == -1)
			return ret;

		break;


	case PMKSA:

		PRINT_D(HOSTINF_DBG, "Handling PMKSA key\n");

		pu8keybuf = kmalloc((pstrHostIFkeyAttr->attr.pmkid.numpmkid * PMKSA_KEY_LEN) + 1, GFP_KERNEL);
		if (pu8keybuf == NULL) {
			PRINT_ER("No buffer to send PMKSA Key\n");
			return -1;
		}

		pu8keybuf[0] = pstrHostIFkeyAttr->attr.pmkid.numpmkid;

		for (i = 0; i < pstrHostIFkeyAttr->attr.pmkid.numpmkid; i++) {
			memcpy(pu8keybuf + ((PMKSA_KEY_LEN * i) + 1), pstrHostIFkeyAttr->attr.pmkid.pmkidlist[i].bssid, ETH_ALEN);
			memcpy(pu8keybuf + ((PMKSA_KEY_LEN * i) + ETH_ALEN + 1), pstrHostIFkeyAttr->attr.pmkid.pmkidlist[i].pmkid, PMKID_LEN);
		}

		strWID.id = (u16)WID_PMKID_INFO;
		strWID.type = WID_STR;
		strWID.val = (s8 *)pu8keybuf;
		strWID.size = (pstrHostIFkeyAttr->attr.pmkid.numpmkid * PMKSA_KEY_LEN) + 1;

		s32Error = send_config_pkt(SET_CFG, &strWID, 1,
					   get_id_from_handler(hif_drv));

		kfree(pu8keybuf);
		break;
	}

	if (s32Error)
		PRINT_ER("Failed to send key config packet\n");


	return s32Error;
}

static void Handle_Disconnect(struct host_if_drv *hif_drv)
{
	struct wid strWID;

	s32 s32Error = 0;
	u16 u16DummyReasonCode = 0;

	strWID.id = (u16)WID_DISCONNECT;
	strWID.type = WID_CHAR;
	strWID.val = (s8 *)&u16DummyReasonCode;
	strWID.size = sizeof(char);



	PRINT_D(HOSTINF_DBG, "Sending disconnect request\n");

	g_obtainingIP = false;
	host_int_set_power_mgmt(hif_drv, 0, 0);

	eth_zero_addr(u8ConnectedSSID);

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));

	if (s32Error) {
		PRINT_ER("Failed to send dissconect config packet\n");
	} else {
		tstrDisconnectNotifInfo strDisconnectNotifInfo;

		memset(&strDisconnectNotifInfo, 0, sizeof(tstrDisconnectNotifInfo));

		strDisconnectNotifInfo.u16reason = 0;
		strDisconnectNotifInfo.ie = NULL;
		strDisconnectNotifInfo.ie_len = 0;

		if (hif_drv->strWILC_UsrScanReq.pfUserScanResult) {
			del_timer(&hif_drv->hScanTimer);
			hif_drv->strWILC_UsrScanReq.pfUserScanResult(SCAN_EVENT_ABORTED, NULL,
									hif_drv->strWILC_UsrScanReq.u32UserScanPvoid, NULL);

			hif_drv->strWILC_UsrScanReq.pfUserScanResult = NULL;
		}

		if (hif_drv->strWILC_UsrConnReq.pfUserConnectResult != NULL)	{
			if (hif_drv->enuHostIFstate == HOST_IF_WAITING_CONN_RESP) {
				PRINT_D(HOSTINF_DBG, "Upper layer requested termination of connection\n");
				del_timer(&hif_drv->hConnectTimer);
			}

			hif_drv->strWILC_UsrConnReq.pfUserConnectResult(CONN_DISCONN_EVENT_DISCONN_NOTIF, NULL,
									   0, &strDisconnectNotifInfo, hif_drv->strWILC_UsrConnReq.u32UserConnectPvoid);
		} else {
			PRINT_ER("strWILC_UsrConnReq.pfUserConnectResult = NULL\n");
		}

		gbScanWhileConnected = false;

		hif_drv->enuHostIFstate = HOST_IF_IDLE;

		eth_zero_addr(hif_drv->au8AssociatedBSSID);

		hif_drv->strWILC_UsrConnReq.ssidLen = 0;
		kfree(hif_drv->strWILC_UsrConnReq.pu8ssid);
		kfree(hif_drv->strWILC_UsrConnReq.pu8bssid);
		hif_drv->strWILC_UsrConnReq.ConnReqIEsLen = 0;
		kfree(hif_drv->strWILC_UsrConnReq.pu8ConnReqIEs);

		if (gu8FlushedJoinReq != NULL && gu8FlushedJoinReqDrvHandler == hif_drv) {
			kfree(gu8FlushedJoinReq);
			gu8FlushedJoinReq = NULL;
		}
		if (gu8FlushedInfoElemAsoc != NULL && gu8FlushedJoinReqDrvHandler == hif_drv) {
			kfree(gu8FlushedInfoElemAsoc);
			gu8FlushedInfoElemAsoc = NULL;
		}

	}

	up(&hif_drv->hSemTestDisconnectBlock);
}


void resolve_disconnect_aberration(struct host_if_drv *hif_drv)
{
	if (!hif_drv)
		return;
	if ((hif_drv->enuHostIFstate == HOST_IF_WAITING_CONN_RESP) || (hif_drv->enuHostIFstate == HOST_IF_CONNECTING)) {
		PRINT_D(HOSTINF_DBG, "\n\n<< correcting Supplicant state machine >>\n\n");
		host_int_disconnect(hif_drv, 1);
	}
}

static s32 Handle_GetChnl(struct host_if_drv *hif_drv)
{

	s32 s32Error = 0;
	struct wid strWID;

	strWID.id = (u16)WID_CURRENT_CHANNEL;
	strWID.type = WID_CHAR;
	strWID.val = (s8 *)&gu8Chnl;
	strWID.size = sizeof(char);

	PRINT_D(HOSTINF_DBG, "Getting channel value\n");

	s32Error = send_config_pkt(GET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));

	if (s32Error) {
		PRINT_ER("Failed to get channel number\n");
		s32Error = -EFAULT;
	}

	up(&hif_drv->hSemGetCHNL);

	return s32Error;



}

static void Handle_GetRssi(struct host_if_drv *hif_drv)
{
	s32 s32Error = 0;
	struct wid strWID;

	strWID.id = (u16)WID_RSSI;
	strWID.type = WID_CHAR;
	strWID.val = &gs8Rssi;
	strWID.size = sizeof(char);

	PRINT_D(HOSTINF_DBG, "Getting RSSI value\n");

	s32Error = send_config_pkt(GET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error) {
		PRINT_ER("Failed to get RSSI value\n");
		s32Error = -EFAULT;
	}

	up(&hif_drv->hSemGetRSSI);


}


static void Handle_GetLinkspeed(struct host_if_drv *hif_drv)
{
	s32 s32Error = 0;
	struct wid strWID;

	gs8lnkspd = 0;

	strWID.id = (u16)WID_LINKSPEED;
	strWID.type = WID_CHAR;
	strWID.val = &gs8lnkspd;
	strWID.size = sizeof(char);

	PRINT_D(HOSTINF_DBG, "Getting LINKSPEED value\n");

	s32Error = send_config_pkt(GET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error) {
		PRINT_ER("Failed to get LINKSPEED value\n");
		s32Error = -EFAULT;
	}

	up(&(hif_drv->hSemGetLINKSPEED));


}

s32 Handle_GetStatistics(struct host_if_drv *hif_drv, struct rf_info *pstrStatistics)
{
	struct wid strWIDList[5];
	u32 u32WidsCount = 0, s32Error = 0;

	strWIDList[u32WidsCount].id = WID_LINKSPEED;
	strWIDList[u32WidsCount].type = WID_CHAR;
	strWIDList[u32WidsCount].size = sizeof(char);
	strWIDList[u32WidsCount].val = (s8 *)(&(pstrStatistics->u8LinkSpeed));
	u32WidsCount++;

	strWIDList[u32WidsCount].id = WID_RSSI;
	strWIDList[u32WidsCount].type = WID_CHAR;
	strWIDList[u32WidsCount].size = sizeof(char);
	strWIDList[u32WidsCount].val = (s8 *)(&(pstrStatistics->s8RSSI));
	u32WidsCount++;

	strWIDList[u32WidsCount].id = WID_SUCCESS_FRAME_COUNT;
	strWIDList[u32WidsCount].type = WID_INT;
	strWIDList[u32WidsCount].size = sizeof(u32);
	strWIDList[u32WidsCount].val = (s8 *)(&(pstrStatistics->u32TxCount));
	u32WidsCount++;

	strWIDList[u32WidsCount].id = WID_RECEIVED_FRAGMENT_COUNT;
	strWIDList[u32WidsCount].type = WID_INT;
	strWIDList[u32WidsCount].size = sizeof(u32);
	strWIDList[u32WidsCount].val = (s8 *)(&(pstrStatistics->u32RxCount));
	u32WidsCount++;

	strWIDList[u32WidsCount].id = WID_FAILED_COUNT;
	strWIDList[u32WidsCount].type = WID_INT;
	strWIDList[u32WidsCount].size = sizeof(u32);
	strWIDList[u32WidsCount].val = (s8 *)(&(pstrStatistics->u32TxFailureCount));
	u32WidsCount++;

	s32Error = send_config_pkt(GET_CFG, strWIDList, u32WidsCount,
				   get_id_from_handler(hif_drv));

	if (s32Error)
		PRINT_ER("Failed to send scan paramters config packet\n");

	up(&hWaitResponse);
	return 0;

}

static s32 Handle_Get_InActiveTime(struct host_if_drv *hif_drv,
				   struct sta_inactive_t *strHostIfStaInactiveT)
{

	s32 s32Error = 0;
	u8 *stamac;
	struct wid strWID;

	strWID.id = (u16)WID_SET_STA_MAC_INACTIVE_TIME;
	strWID.type = WID_STR;
	strWID.size = ETH_ALEN;
	strWID.val = kmalloc(strWID.size, GFP_KERNEL);


	stamac = strWID.val;
	memcpy(stamac, strHostIfStaInactiveT->mac, ETH_ALEN);


	PRINT_D(CFG80211_DBG, "SETING STA inactive time\n");


	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));

	if (s32Error) {
		PRINT_ER("Failed to SET incative time\n");
		return -EFAULT;
	}


	strWID.id = (u16)WID_GET_INACTIVE_TIME;
	strWID.type = WID_INT;
	strWID.val = (s8 *)&gu32InactiveTime;
	strWID.size = sizeof(u32);


	s32Error = send_config_pkt(GET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));

	if (s32Error) {
		PRINT_ER("Failed to get incative time\n");
		return -EFAULT;
	}


	PRINT_D(CFG80211_DBG, "Getting inactive time : %d\n", gu32InactiveTime);

	up(&hif_drv->hSemInactiveTime);

	return s32Error;



}

static void Handle_AddBeacon(struct host_if_drv *hif_drv,
			     struct beacon_attr *pstrSetBeaconParam)
{
	s32 s32Error = 0;
	struct wid strWID;
	u8 *pu8CurrByte;

	PRINT_D(HOSTINF_DBG, "Adding BEACON\n");

	strWID.id = (u16)WID_ADD_BEACON;
	strWID.type = WID_BIN;
	strWID.size = pstrSetBeaconParam->head_len + pstrSetBeaconParam->tail_len + 16;
	strWID.val = kmalloc(strWID.size, GFP_KERNEL);
	if (strWID.val == NULL)
		goto ERRORHANDLER;

	pu8CurrByte = strWID.val;
	*pu8CurrByte++ = (pstrSetBeaconParam->interval & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->interval >> 8) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->interval >> 16) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->interval >> 24) & 0xFF);

	*pu8CurrByte++ = (pstrSetBeaconParam->dtim_period & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->dtim_period >> 8) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->dtim_period >> 16) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->dtim_period >> 24) & 0xFF);

	*pu8CurrByte++ = (pstrSetBeaconParam->head_len & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->head_len >> 8) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->head_len >> 16) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->head_len >> 24) & 0xFF);

	memcpy(pu8CurrByte, pstrSetBeaconParam->head, pstrSetBeaconParam->head_len);
	pu8CurrByte += pstrSetBeaconParam->head_len;

	*pu8CurrByte++ = (pstrSetBeaconParam->tail_len & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->tail_len >> 8) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->tail_len >> 16) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->tail_len >> 24) & 0xFF);

	if (pstrSetBeaconParam->tail > 0)
		memcpy(pu8CurrByte, pstrSetBeaconParam->tail, pstrSetBeaconParam->tail_len);
	pu8CurrByte += pstrSetBeaconParam->tail_len;

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error)
		PRINT_ER("Failed to send add beacon config packet\n");

ERRORHANDLER:
	kfree(strWID.val);
	kfree(pstrSetBeaconParam->head);
	kfree(pstrSetBeaconParam->tail);
}

static void Handle_DelBeacon(struct host_if_drv *hif_drv)
{
	s32 s32Error = 0;
	struct wid strWID;
	u8 *pu8CurrByte;

	strWID.id = (u16)WID_DEL_BEACON;
	strWID.type = WID_CHAR;
	strWID.size = sizeof(char);
	strWID.val = &gu8DelBcn;

	if (strWID.val == NULL)
		return;

	pu8CurrByte = strWID.val;

	PRINT_D(HOSTINF_DBG, "Deleting BEACON\n");

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error)
		PRINT_ER("Failed to send delete beacon config packet\n");
}

static u32 WILC_HostIf_PackStaParam(u8 *pu8Buffer,
				    struct add_sta_param *pstrStationParam)
{
	u8 *pu8CurrByte;

	pu8CurrByte = pu8Buffer;

	PRINT_D(HOSTINF_DBG, "Packing STA params\n");
	memcpy(pu8CurrByte, pstrStationParam->au8BSSID, ETH_ALEN);
	pu8CurrByte +=  ETH_ALEN;

	*pu8CurrByte++ = pstrStationParam->u16AssocID & 0xFF;
	*pu8CurrByte++ = (pstrStationParam->u16AssocID >> 8) & 0xFF;

	*pu8CurrByte++ = pstrStationParam->u8NumRates;
	if (pstrStationParam->u8NumRates > 0)
		memcpy(pu8CurrByte, pstrStationParam->pu8Rates, pstrStationParam->u8NumRates);
	pu8CurrByte += pstrStationParam->u8NumRates;

	*pu8CurrByte++ = pstrStationParam->bIsHTSupported;
	*pu8CurrByte++ = pstrStationParam->u16HTCapInfo & 0xFF;
	*pu8CurrByte++ = (pstrStationParam->u16HTCapInfo >> 8) & 0xFF;

	*pu8CurrByte++ = pstrStationParam->u8AmpduParams;
	memcpy(pu8CurrByte, pstrStationParam->au8SuppMCsSet, WILC_SUPP_MCS_SET_SIZE);
	pu8CurrByte += WILC_SUPP_MCS_SET_SIZE;

	*pu8CurrByte++ = pstrStationParam->u16HTExtParams & 0xFF;
	*pu8CurrByte++ = (pstrStationParam->u16HTExtParams >> 8) & 0xFF;

	*pu8CurrByte++ = pstrStationParam->u32TxBeamformingCap & 0xFF;
	*pu8CurrByte++ = (pstrStationParam->u32TxBeamformingCap >> 8) & 0xFF;
	*pu8CurrByte++ = (pstrStationParam->u32TxBeamformingCap >> 16) & 0xFF;
	*pu8CurrByte++ = (pstrStationParam->u32TxBeamformingCap >> 24) & 0xFF;

	*pu8CurrByte++ = pstrStationParam->u8ASELCap;

	*pu8CurrByte++ = pstrStationParam->u16FlagsMask & 0xFF;
	*pu8CurrByte++ = (pstrStationParam->u16FlagsMask >> 8) & 0xFF;

	*pu8CurrByte++ = pstrStationParam->u16FlagsSet & 0xFF;
	*pu8CurrByte++ = (pstrStationParam->u16FlagsSet >> 8) & 0xFF;

	return pu8CurrByte - pu8Buffer;
}

static void Handle_AddStation(struct host_if_drv *hif_drv,
			      struct add_sta_param *pstrStationParam)
{
	s32 s32Error = 0;
	struct wid strWID;
	u8 *pu8CurrByte;

	PRINT_D(HOSTINF_DBG, "Handling add station\n");
	strWID.id = (u16)WID_ADD_STA;
	strWID.type = WID_BIN;
	strWID.size = WILC_ADD_STA_LENGTH + pstrStationParam->u8NumRates;

	strWID.val = kmalloc(strWID.size, GFP_KERNEL);
	if (strWID.val == NULL)
		goto ERRORHANDLER;

	pu8CurrByte = strWID.val;
	pu8CurrByte += WILC_HostIf_PackStaParam(pu8CurrByte, pstrStationParam);

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error != 0)
		PRINT_ER("Failed to send add station config packet\n");

ERRORHANDLER:
	kfree(pstrStationParam->pu8Rates);
	kfree(strWID.val);
}

static void Handle_DelAllSta(struct host_if_drv *hif_drv,
			     struct del_all_sta *pstrDelAllStaParam)
{
	s32 s32Error = 0;

	struct wid strWID;
	u8 *pu8CurrByte;
	u8 i;
	u8 au8Zero_Buff[6] = {0};

	strWID.id = (u16)WID_DEL_ALL_STA;
	strWID.type = WID_STR;
	strWID.size = (pstrDelAllStaParam->u8Num_AssocSta * ETH_ALEN) + 1;

	PRINT_D(HOSTINF_DBG, "Handling delete station\n");

	strWID.val = kmalloc((pstrDelAllStaParam->u8Num_AssocSta * ETH_ALEN) + 1, GFP_KERNEL);
	if (strWID.val == NULL)
		goto ERRORHANDLER;

	pu8CurrByte = strWID.val;

	*(pu8CurrByte++) = pstrDelAllStaParam->u8Num_AssocSta;

	for (i = 0; i < MAX_NUM_STA; i++) {
		if (memcmp(pstrDelAllStaParam->au8Sta_DelAllSta[i], au8Zero_Buff, ETH_ALEN))
			memcpy(pu8CurrByte, pstrDelAllStaParam->au8Sta_DelAllSta[i], ETH_ALEN);
		else
			continue;

		pu8CurrByte += ETH_ALEN;
	}

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error)
		PRINT_ER("Failed to send add station config packet\n");

ERRORHANDLER:
	kfree(strWID.val);

	up(&hWaitResponse);
}

static void Handle_DelStation(struct host_if_drv *hif_drv,
			      struct del_sta *pstrDelStaParam)
{
	s32 s32Error = 0;
	struct wid strWID;
	u8 *pu8CurrByte;

	strWID.id = (u16)WID_REMOVE_STA;
	strWID.type = WID_BIN;
	strWID.size = ETH_ALEN;

	PRINT_D(HOSTINF_DBG, "Handling delete station\n");

	strWID.val = kmalloc(strWID.size, GFP_KERNEL);
	if (strWID.val == NULL)
		goto ERRORHANDLER;

	pu8CurrByte = strWID.val;

	memcpy(pu8CurrByte, pstrDelStaParam->au8MacAddr, ETH_ALEN);

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error)
		PRINT_ER("Failed to send add station config packet\n");

ERRORHANDLER:
	kfree(strWID.val);
}

static void Handle_EditStation(struct host_if_drv *hif_drv,
			       struct add_sta_param *pstrStationParam)
{
	s32 s32Error = 0;
	struct wid strWID;
	u8 *pu8CurrByte;

	strWID.id = (u16)WID_EDIT_STA;
	strWID.type = WID_BIN;
	strWID.size = WILC_ADD_STA_LENGTH + pstrStationParam->u8NumRates;

	PRINT_D(HOSTINF_DBG, "Handling edit station\n");
	strWID.val = kmalloc(strWID.size, GFP_KERNEL);
	if (strWID.val == NULL)
		goto ERRORHANDLER;

	pu8CurrByte = strWID.val;
	pu8CurrByte += WILC_HostIf_PackStaParam(pu8CurrByte, pstrStationParam);

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error)
		PRINT_ER("Failed to send edit station config packet\n");

ERRORHANDLER:
	kfree(pstrStationParam->pu8Rates);
	kfree(strWID.val);
}

static int Handle_RemainOnChan(struct host_if_drv *hif_drv,
			       struct remain_ch *pstrHostIfRemainOnChan)
{
	s32 s32Error = 0;
	u8 u8remain_on_chan_flag;
	struct wid strWID;

	if (!hif_drv->u8RemainOnChan_pendingreq) {
		hif_drv->strHostIfRemainOnChan.pVoid = pstrHostIfRemainOnChan->pVoid;
		hif_drv->strHostIfRemainOnChan.pRemainOnChanExpired = pstrHostIfRemainOnChan->pRemainOnChanExpired;
		hif_drv->strHostIfRemainOnChan.pRemainOnChanReady = pstrHostIfRemainOnChan->pRemainOnChanReady;
		hif_drv->strHostIfRemainOnChan.u16Channel = pstrHostIfRemainOnChan->u16Channel;
		hif_drv->strHostIfRemainOnChan.u32ListenSessionID = pstrHostIfRemainOnChan->u32ListenSessionID;
	} else {
		pstrHostIfRemainOnChan->u16Channel = hif_drv->strHostIfRemainOnChan.u16Channel;
	}

	if (hif_drv->strWILC_UsrScanReq.pfUserScanResult != NULL) {
		PRINT_INFO(GENERIC_DBG, "Required to remain on chan while scanning return\n");
		hif_drv->u8RemainOnChan_pendingreq = 1;
		s32Error = -EBUSY;
		goto ERRORHANDLER;
	}
	if (hif_drv->enuHostIFstate == HOST_IF_WAITING_CONN_RESP) {
		PRINT_INFO(GENERIC_DBG, "Required to remain on chan while connecting return\n");
		s32Error = -EBUSY;
		goto ERRORHANDLER;
	}

	if (g_obtainingIP || connecting) {
		PRINT_D(GENERIC_DBG, "[handle_scan]: Don't do obss scan until IP adresss is obtained\n");
		s32Error = -EBUSY;
		goto ERRORHANDLER;
	}

	PRINT_D(HOSTINF_DBG, "Setting channel :%d\n", pstrHostIfRemainOnChan->u16Channel);

	u8remain_on_chan_flag = true;
	strWID.id = (u16)WID_REMAIN_ON_CHAN;
	strWID.type = WID_STR;
	strWID.size = 2;
	strWID.val = kmalloc(strWID.size, GFP_KERNEL);

	if (strWID.val == NULL) {
		s32Error = -ENOMEM;
		goto ERRORHANDLER;
	}

	strWID.val[0] = u8remain_on_chan_flag;
	strWID.val[1] = (s8)pstrHostIfRemainOnChan->u16Channel;

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error != 0)
		PRINT_ER("Failed to set remain on channel\n");

ERRORHANDLER:
	{
		P2P_LISTEN_STATE = 1;
		hif_drv->hRemainOnChannel.data = (unsigned long)hif_drv;
		mod_timer(&hif_drv->hRemainOnChannel,
			  jiffies +
			  msecs_to_jiffies(pstrHostIfRemainOnChan->u32duration));

		if (hif_drv->strHostIfRemainOnChan.pRemainOnChanReady)
			hif_drv->strHostIfRemainOnChan.pRemainOnChanReady(hif_drv->strHostIfRemainOnChan.pVoid);

		if (hif_drv->u8RemainOnChan_pendingreq)
			hif_drv->u8RemainOnChan_pendingreq = 0;
	}
	return s32Error;
}

static int Handle_RegisterFrame(struct host_if_drv *hif_drv,
				struct reg_frame *pstrHostIfRegisterFrame)
{
	s32 s32Error = 0;
	struct wid strWID;
	u8 *pu8CurrByte;

	PRINT_D(HOSTINF_DBG, "Handling frame register Flag : %d FrameType: %d\n", pstrHostIfRegisterFrame->bReg, pstrHostIfRegisterFrame->u16FrameType);

	strWID.id = (u16)WID_REGISTER_FRAME;
	strWID.type = WID_STR;
	strWID.val = kmalloc(sizeof(u16) + 2, GFP_KERNEL);
	if (strWID.val == NULL)
		return -ENOMEM;

	pu8CurrByte = strWID.val;

	*pu8CurrByte++ = pstrHostIfRegisterFrame->bReg;
	*pu8CurrByte++ = pstrHostIfRegisterFrame->u8Regid;
	memcpy(pu8CurrByte, &(pstrHostIfRegisterFrame->u16FrameType), sizeof(u16));


	strWID.size = sizeof(u16) + 2;

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error) {
		PRINT_ER("Failed to frame register config packet\n");
		s32Error = -EINVAL;
	}

	return s32Error;

}

#define FALSE_FRMWR_CHANNEL 100
static u32 Handle_ListenStateExpired(struct host_if_drv *hif_drv,
				     struct remain_ch *pstrHostIfRemainOnChan)
{
	u8 u8remain_on_chan_flag;
	struct wid strWID;
	s32 s32Error = 0;

	PRINT_D(HOSTINF_DBG, "CANCEL REMAIN ON CHAN\n");

	if (P2P_LISTEN_STATE) {
		u8remain_on_chan_flag = false;
		strWID.id = (u16)WID_REMAIN_ON_CHAN;
		strWID.type = WID_STR;
		strWID.size = 2;
		strWID.val = kmalloc(strWID.size, GFP_KERNEL);

		if (strWID.val == NULL)
			PRINT_ER("Failed to allocate memory\n");

		strWID.val[0] = u8remain_on_chan_flag;
		strWID.val[1] = FALSE_FRMWR_CHANNEL;

		s32Error = send_config_pkt(SET_CFG, &strWID, 1,
					   get_id_from_handler(hif_drv));
		if (s32Error != 0) {
			PRINT_ER("Failed to set remain on channel\n");
			goto _done_;
		}

		if (hif_drv->strHostIfRemainOnChan.pRemainOnChanExpired) {
			hif_drv->strHostIfRemainOnChan.pRemainOnChanExpired(hif_drv->strHostIfRemainOnChan.pVoid
									       , pstrHostIfRemainOnChan->u32ListenSessionID);
		}
		P2P_LISTEN_STATE = 0;
	} else {
		PRINT_D(GENERIC_DBG, "Not in listen state\n");
		s32Error = -EFAULT;
	}

_done_:
	return s32Error;
}

static void ListenTimerCB(unsigned long arg)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	struct host_if_drv *hif_drv = (struct host_if_drv *)arg;

	del_timer(&hif_drv->hRemainOnChannel);

	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_LISTEN_TIMER_FIRED;
	msg.drv = hif_drv;
	msg.body.remain_on_ch.u32ListenSessionID = hif_drv->strHostIfRemainOnChan.u32ListenSessionID;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");
}

static void Handle_PowerManagement(struct host_if_drv *hif_drv,
				   struct power_mgmt_param *strPowerMgmtParam)
{
	s32 s32Error = 0;
	struct wid strWID;
	s8 s8PowerMode;

	strWID.id = (u16)WID_POWER_MANAGEMENT;

	if (strPowerMgmtParam->bIsEnabled == true)
		s8PowerMode = MIN_FAST_PS;
	else
		s8PowerMode = NO_POWERSAVE;
	PRINT_D(HOSTINF_DBG, "Handling power mgmt to %d\n", s8PowerMode);
	strWID.val = &s8PowerMode;
	strWID.size = sizeof(char);

	PRINT_D(HOSTINF_DBG, "Handling Power Management\n");

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error)
		PRINT_ER("Failed to send power management config packet\n");
}

static void Handle_SetMulticastFilter(struct host_if_drv *hif_drv,
				      struct set_multicast *strHostIfSetMulti)
{
	s32 s32Error = 0;
	struct wid strWID;
	u8 *pu8CurrByte;

	PRINT_D(HOSTINF_DBG, "Setup Multicast Filter\n");

	strWID.id = (u16)WID_SETUP_MULTICAST_FILTER;
	strWID.type = WID_BIN;
	strWID.size = sizeof(struct set_multicast) + ((strHostIfSetMulti->u32count) * ETH_ALEN);
	strWID.val = kmalloc(strWID.size, GFP_KERNEL);
	if (strWID.val == NULL)
		goto ERRORHANDLER;

	pu8CurrByte = strWID.val;
	*pu8CurrByte++ = (strHostIfSetMulti->bIsEnabled & 0xFF);
	*pu8CurrByte++ = ((strHostIfSetMulti->bIsEnabled >> 8) & 0xFF);
	*pu8CurrByte++ = ((strHostIfSetMulti->bIsEnabled >> 16) & 0xFF);
	*pu8CurrByte++ = ((strHostIfSetMulti->bIsEnabled >> 24) & 0xFF);

	*pu8CurrByte++ = (strHostIfSetMulti->u32count & 0xFF);
	*pu8CurrByte++ = ((strHostIfSetMulti->u32count >> 8) & 0xFF);
	*pu8CurrByte++ = ((strHostIfSetMulti->u32count >> 16) & 0xFF);
	*pu8CurrByte++ = ((strHostIfSetMulti->u32count >> 24) & 0xFF);

	if ((strHostIfSetMulti->u32count) > 0)
		memcpy(pu8CurrByte, gau8MulticastMacAddrList, ((strHostIfSetMulti->u32count) * ETH_ALEN));

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error)
		PRINT_ER("Failed to send setup multicast config packet\n");

ERRORHANDLER:
	kfree(strWID.val);

}

static s32 Handle_AddBASession(struct host_if_drv *hif_drv,
			       struct ba_session_info *strHostIfBASessionInfo)
{
	s32 s32Error = 0;
	struct wid strWID;
	int AddbaTimeout = 100;
	char *ptr = NULL;

	PRINT_D(HOSTINF_DBG, "Opening Block Ack session with\nBSSID = %.2x:%.2x:%.2x\nTID=%d\nBufferSize == %d\nSessionTimeOut = %d\n",
		strHostIfBASessionInfo->au8Bssid[0],
		strHostIfBASessionInfo->au8Bssid[1],
		strHostIfBASessionInfo->au8Bssid[2],
		strHostIfBASessionInfo->u16BufferSize,
		strHostIfBASessionInfo->u16SessionTimeout,
		strHostIfBASessionInfo->u8Ted);

	strWID.id = (u16)WID_11E_P_ACTION_REQ;
	strWID.type = WID_STR;
	strWID.val = kmalloc(BLOCK_ACK_REQ_SIZE, GFP_KERNEL);
	strWID.size = BLOCK_ACK_REQ_SIZE;
	ptr = strWID.val;
	*ptr++ = 0x14;
	*ptr++ = 0x3;
	*ptr++ = 0x0;
	memcpy(ptr, strHostIfBASessionInfo->au8Bssid, ETH_ALEN);
	ptr += ETH_ALEN;
	*ptr++ = strHostIfBASessionInfo->u8Ted;
	*ptr++ = 1;
	*ptr++ = (strHostIfBASessionInfo->u16BufferSize & 0xFF);
	*ptr++ = ((strHostIfBASessionInfo->u16BufferSize >> 16) & 0xFF);
	*ptr++ = (strHostIfBASessionInfo->u16SessionTimeout & 0xFF);
	*ptr++ = ((strHostIfBASessionInfo->u16SessionTimeout >> 16) & 0xFF);
	*ptr++ = (AddbaTimeout & 0xFF);
	*ptr++ = ((AddbaTimeout >> 16) & 0xFF);
	*ptr++ = 8;
	*ptr++ = 0;

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error)
		PRINT_D(HOSTINF_DBG, "Couldn't open BA Session\n");


	strWID.id = (u16)WID_11E_P_ACTION_REQ;
	strWID.type = WID_STR;
	strWID.size = 15;
	ptr = strWID.val;
	*ptr++ = 15;
	*ptr++ = 7;
	*ptr++ = 0x2;
	memcpy(ptr, strHostIfBASessionInfo->au8Bssid, ETH_ALEN);
	ptr += ETH_ALEN;
	*ptr++ = strHostIfBASessionInfo->u8Ted;
	*ptr++ = 8;
	*ptr++ = (strHostIfBASessionInfo->u16BufferSize & 0xFF);
	*ptr++ = ((strHostIfBASessionInfo->u16SessionTimeout >> 16) & 0xFF);
	*ptr++ = 3;
	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));

	if (strWID.val != NULL)
		kfree(strWID.val);

	return s32Error;

}

static s32 Handle_DelAllRxBASessions(struct host_if_drv *hif_drv,
				     struct ba_session_info *strHostIfBASessionInfo)
{
	s32 s32Error = 0;
	struct wid strWID;
	char *ptr = NULL;

	PRINT_D(GENERIC_DBG, "Delete Block Ack session with\nBSSID = %.2x:%.2x:%.2x\nTID=%d\n",
		strHostIfBASessionInfo->au8Bssid[0],
		strHostIfBASessionInfo->au8Bssid[1],
		strHostIfBASessionInfo->au8Bssid[2],
		strHostIfBASessionInfo->u8Ted);

	strWID.id = (u16)WID_DEL_ALL_RX_BA;
	strWID.type = WID_STR;
	strWID.val = kmalloc(BLOCK_ACK_REQ_SIZE, GFP_KERNEL);
	strWID.size = BLOCK_ACK_REQ_SIZE;
	ptr = strWID.val;
	*ptr++ = 0x14;
	*ptr++ = 0x3;
	*ptr++ = 0x2;
	memcpy(ptr, strHostIfBASessionInfo->au8Bssid, ETH_ALEN);
	ptr += ETH_ALEN;
	*ptr++ = strHostIfBASessionInfo->u8Ted;
	*ptr++ = 0;
	*ptr++ = 32;

	s32Error = send_config_pkt(SET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error)
		PRINT_D(HOSTINF_DBG, "Couldn't delete BA Session\n");


	if (strWID.val != NULL)
		kfree(strWID.val);

	up(&hWaitResponse);

	return s32Error;

}

static int hostIFthread(void *pvArg)
{
	u32 u32Ret;
	struct host_if_msg msg;
	struct host_if_drv *hif_drv;

	memset(&msg, 0, sizeof(struct host_if_msg));

	while (1) {
		wilc_mq_recv(&gMsgQHostIF, &msg, sizeof(struct host_if_msg), &u32Ret);
		hif_drv = (struct host_if_drv *)msg.drv;
		if (msg.id == HOST_IF_MSG_EXIT) {
			PRINT_D(GENERIC_DBG, "THREAD: Exiting HostIfThread\n");
			break;
		}

		if ((!g_wilc_initialized)) {
			PRINT_D(GENERIC_DBG, "--WAIT--");
			usleep_range(200 * 1000, 200 * 1000);
			wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
			continue;
		}

		if (msg.id == HOST_IF_MSG_CONNECT && hif_drv->strWILC_UsrScanReq.pfUserScanResult != NULL) {
			PRINT_D(HOSTINF_DBG, "Requeue connect request till scan done received\n");
			wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
			usleep_range(2 * 1000, 2 * 1000);
			continue;
		}

		switch (msg.id) {
		case HOST_IF_MSG_Q_IDLE:
			Handle_wait_msg_q_empty();
			break;

		case HOST_IF_MSG_SCAN:
			Handle_Scan(msg.drv, &msg.body.scan_info);
			break;

		case HOST_IF_MSG_CONNECT:
			Handle_Connect(msg.drv, &msg.body.con_info);
			break;

		case HOST_IF_MSG_FLUSH_CONNECT:
			Handle_FlushConnect(msg.drv);
			break;

		case HOST_IF_MSG_RCVD_NTWRK_INFO:
			Handle_RcvdNtwrkInfo(msg.drv, &msg.body.net_info);
			break;

		case HOST_IF_MSG_RCVD_GNRL_ASYNC_INFO:
			Handle_RcvdGnrlAsyncInfo(msg.drv, &msg.body.async_info);
			break;

		case HOST_IF_MSG_KEY:
			Handle_Key(msg.drv, &msg.body.key_info);
			break;

		case HOST_IF_MSG_CFG_PARAMS:

			Handle_CfgParam(msg.drv, &msg.body.cfg_info);
			break;

		case HOST_IF_MSG_SET_CHANNEL:
			Handle_SetChannel(msg.drv, &msg.body.channel_info);
			break;

		case HOST_IF_MSG_DISCONNECT:
			Handle_Disconnect(msg.drv);
			break;

		case HOST_IF_MSG_RCVD_SCAN_COMPLETE:
			del_timer(&hif_drv->hScanTimer);
			PRINT_D(HOSTINF_DBG, "scan completed successfully\n");

			if (!linux_wlan_get_num_conn_ifcs())
				chip_sleep_manually(INFINITE_SLEEP_TIME);

			Handle_ScanDone(msg.drv, SCAN_EVENT_DONE);

			if (hif_drv->u8RemainOnChan_pendingreq)
				Handle_RemainOnChan(msg.drv, &msg.body.remain_on_ch);

			break;

		case HOST_IF_MSG_GET_RSSI:
			Handle_GetRssi(msg.drv);
			break;

		case HOST_IF_MSG_GET_LINKSPEED:
			Handle_GetLinkspeed(msg.drv);
			break;

		case HOST_IF_MSG_GET_STATISTICS:
			Handle_GetStatistics(msg.drv, (struct rf_info *)msg.body.data);
			break;

		case HOST_IF_MSG_GET_CHNL:
			Handle_GetChnl(msg.drv);
			break;

		case HOST_IF_MSG_ADD_BEACON:
			Handle_AddBeacon(msg.drv, &msg.body.beacon_info);
			break;

		case HOST_IF_MSG_DEL_BEACON:
			Handle_DelBeacon(msg.drv);
			break;

		case HOST_IF_MSG_ADD_STATION:
			Handle_AddStation(msg.drv, &msg.body.add_sta_info);
			break;

		case HOST_IF_MSG_DEL_STATION:
			Handle_DelStation(msg.drv, &msg.body.del_sta_info);
			break;

		case HOST_IF_MSG_EDIT_STATION:
			Handle_EditStation(msg.drv, &msg.body.edit_sta_info);
			break;

		case HOST_IF_MSG_GET_INACTIVETIME:
			Handle_Get_InActiveTime(msg.drv, &msg.body.mac_info);
			break;

		case HOST_IF_MSG_SCAN_TIMER_FIRED:
			PRINT_D(HOSTINF_DBG, "Scan Timeout\n");

			Handle_ScanDone(msg.drv, SCAN_EVENT_ABORTED);
			break;

		case HOST_IF_MSG_CONNECT_TIMER_FIRED:
			PRINT_D(HOSTINF_DBG, "Connect Timeout\n");
			Handle_ConnectTimeout(msg.drv);
			break;

		case HOST_IF_MSG_POWER_MGMT:
			Handle_PowerManagement(msg.drv, &msg.body.pwr_mgmt_info);
			break;

		case HOST_IF_MSG_SET_WFIDRV_HANDLER:
			Handle_SetWfiDrvHandler(msg.drv,
						&msg.body.drv);
			break;

		case HOST_IF_MSG_SET_OPERATION_MODE:
			Handle_SetOperationMode(msg.drv, &msg.body.mode);
			break;

		case HOST_IF_MSG_SET_IPADDRESS:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_SET_IPADDRESS\n");
			Handle_set_IPAddress(msg.drv, msg.body.ip_info.au8IPAddr, msg.body.ip_info.idx);
			break;

		case HOST_IF_MSG_GET_IPADDRESS:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_SET_IPADDRESS\n");
			Handle_get_IPAddress(msg.drv, msg.body.ip_info.au8IPAddr, msg.body.ip_info.idx);
			break;

		case HOST_IF_MSG_SET_MAC_ADDRESS:
			Handle_SetMacAddress(msg.drv, &msg.body.set_mac_info);
			break;

		case HOST_IF_MSG_GET_MAC_ADDRESS:
			Handle_GetMacAddress(msg.drv, &msg.body.get_mac_info);
			break;

		case HOST_IF_MSG_REMAIN_ON_CHAN:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_REMAIN_ON_CHAN\n");
			Handle_RemainOnChan(msg.drv, &msg.body.remain_on_ch);
			break;

		case HOST_IF_MSG_REGISTER_FRAME:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_REGISTER_FRAME\n");
			Handle_RegisterFrame(msg.drv, &msg.body.reg_frame);
			break;

		case HOST_IF_MSG_LISTEN_TIMER_FIRED:
			Handle_ListenStateExpired(msg.drv, &msg.body.remain_on_ch);
			break;

		case HOST_IF_MSG_SET_MULTICAST_FILTER:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_SET_MULTICAST_FILTER\n");
			Handle_SetMulticastFilter(msg.drv, &msg.body.multicast_info);
			break;

		case HOST_IF_MSG_ADD_BA_SESSION:
			Handle_AddBASession(msg.drv, &msg.body.session_info);
			break;

		case HOST_IF_MSG_DEL_ALL_RX_BA_SESSIONS:
			Handle_DelAllRxBASessions(msg.drv, &msg.body.session_info);
			break;

		case HOST_IF_MSG_DEL_ALL_STA:
			Handle_DelAllSta(msg.drv, &msg.body.del_all_sta_info);
			break;

		default:
			PRINT_ER("[Host Interface] undefined Received Msg ID\n");
			break;
		}
	}

	PRINT_D(HOSTINF_DBG, "Releasing thread exit semaphore\n");
	up(&hSemHostIFthrdEnd);
	return 0;
}

static void TimerCB_Scan(unsigned long arg)
{
	void *pvArg = (void *)arg;
	struct host_if_msg msg;

	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.drv = pvArg;
	msg.id = HOST_IF_MSG_SCAN_TIMER_FIRED;

	wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
}

static void TimerCB_Connect(unsigned long arg)
{
	void *pvArg = (void *)arg;
	struct host_if_msg msg;

	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.drv = pvArg;
	msg.id = HOST_IF_MSG_CONNECT_TIMER_FIRED;

	wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
}

s32 host_int_remove_key(struct host_if_drv *hif_drv, const u8 *pu8StaAddress)
{
	struct wid strWID;

	strWID.id = (u16)WID_REMOVE_KEY;
	strWID.type = WID_STR;
	strWID.val = (s8 *)pu8StaAddress;
	strWID.size = 6;

	return 0;
}

int host_int_remove_wep_key(struct host_if_drv *hif_drv, u8 index)
{
	int result = 0;
	struct host_if_msg msg;

	if (!hif_drv) {
		result = -EFAULT;
		PRINT_ER("Failed to send setup multicast config packet\n");
		return result;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_KEY;
	msg.body.key_info.type = WEP;
	msg.body.key_info.action = REMOVEKEY;
	msg.drv = hif_drv;
	msg.body.key_info.attr.wep.index = index;

	result = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (result)
		PRINT_ER("Error in sending message queue : Request to remove WEP key\n");
	down(&hif_drv->hSemTestKeyBlock);

	return result;
}

s32 host_int_set_WEPDefaultKeyID(struct host_if_drv *hif_drv, u8 u8Index)
{
	s32 s32Error = 0;
	struct host_if_msg msg;


	if (!hif_drv) {
		s32Error = -EFAULT;
		PRINT_ER("driver is null\n");
		return s32Error;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));


	msg.id = HOST_IF_MSG_KEY;
	msg.body.key_info.type = WEP;
	msg.body.key_info.action = DEFAULTKEY;
	msg.drv = hif_drv;
	msg.body.key_info.attr.wep.index = u8Index;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Error in sending message queue : Default key index\n");
	down(&hif_drv->hSemTestKeyBlock);

	return s32Error;
}

s32 host_int_add_wep_key_bss_sta(struct host_if_drv *hif_drv,
				 const u8 *pu8WepKey,
				 u8 u8WepKeylen,
				 u8 u8Keyidx)
{

	s32 s32Error = 0;
	struct host_if_msg msg;

	if (!hif_drv) {
		s32Error = -EFAULT;
		PRINT_ER("driver is null\n");
		return s32Error;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));


	msg.id = HOST_IF_MSG_KEY;
	msg.body.key_info.type = WEP;
	msg.body.key_info.action = ADDKEY;
	msg.drv = hif_drv;
	msg.body.key_info.attr.wep.key = kmalloc(u8WepKeylen, GFP_KERNEL);
	memcpy(msg.body.key_info.attr.wep.key, pu8WepKey, u8WepKeylen);
	msg.body.key_info.attr.wep.key_len = (u8WepKeylen);
	msg.body.key_info.attr.wep.index = u8Keyidx;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Error in sending message queue :WEP Key\n");
	down(&hif_drv->hSemTestKeyBlock);

	return s32Error;

}

s32 host_int_add_wep_key_bss_ap(struct host_if_drv *hif_drv,
				const u8 *pu8WepKey,
				u8 u8WepKeylen,
				u8 u8Keyidx,
				u8 u8mode,
				enum AUTHTYPE tenuAuth_type)
{

	s32 s32Error = 0;
	struct host_if_msg msg;
	u8 i;

	if (!hif_drv) {
		s32Error = -EFAULT;
		PRINT_ER("driver is null\n");
		return s32Error;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	if (INFO) {
		for (i = 0; i < u8WepKeylen; i++)
			PRINT_INFO(HOSTAPD_DBG, "KEY is %x\n", pu8WepKey[i]);
	}
	msg.id = HOST_IF_MSG_KEY;
	msg.body.key_info.type = WEP;
	msg.body.key_info.action = ADDKEY_AP;
	msg.drv = hif_drv;
	msg.body.key_info.attr.wep.key = kmalloc(u8WepKeylen, GFP_KERNEL);
	memcpy(msg.body.key_info.attr.wep.key, pu8WepKey, (u8WepKeylen));
	msg.body.key_info.attr.wep.key_len = (u8WepKeylen);
	msg.body.key_info.attr.wep.index = u8Keyidx;
	msg.body.key_info.attr.wep.mode = u8mode;
	msg.body.key_info.attr.wep.auth_type = tenuAuth_type;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));

	if (s32Error)
		PRINT_ER("Error in sending message queue :WEP Key\n");
	down(&hif_drv->hSemTestKeyBlock);

	return s32Error;

}

s32 host_int_add_ptk(struct host_if_drv *hif_drv, const u8 *pu8Ptk,
		     u8 u8PtkKeylen, const u8 *mac_addr,
		     const u8 *pu8RxMic, const u8 *pu8TxMic,
		     u8 mode, u8 u8Ciphermode, u8 u8Idx)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	u8 u8KeyLen = u8PtkKeylen;
	u32 i;

	if (!hif_drv) {
		s32Error = -EFAULT;
		PRINT_ER("driver is null\n");
		return s32Error;
	}
	if (pu8RxMic != NULL)
		u8KeyLen += RX_MIC_KEY_LEN;
	if (pu8TxMic != NULL)
		u8KeyLen += TX_MIC_KEY_LEN;

	memset(&msg, 0, sizeof(struct host_if_msg));


	msg.id = HOST_IF_MSG_KEY;
	msg.body.key_info.type = WPAPtk;
	if (mode == AP_MODE) {
		msg.body.key_info.action = ADDKEY_AP;
		msg.body.key_info.attr.wpa.index = u8Idx;
	}
	if (mode == STATION_MODE)
		msg.body.key_info.action = ADDKEY;

	msg.body.key_info.attr.wpa.key = kmalloc(u8PtkKeylen, GFP_KERNEL);
	memcpy(msg.body.key_info.attr.wpa.key, pu8Ptk, u8PtkKeylen);

	if (pu8RxMic != NULL) {
		memcpy(msg.body.key_info.attr.wpa.key + 16, pu8RxMic, RX_MIC_KEY_LEN);
		if (INFO) {
			for (i = 0; i < RX_MIC_KEY_LEN; i++)
				PRINT_INFO(CFG80211_DBG, "PairwiseRx[%d] = %x\n", i, pu8RxMic[i]);
		}
	}
	if (pu8TxMic != NULL) {
		memcpy(msg.body.key_info.attr.wpa.key + 24, pu8TxMic, TX_MIC_KEY_LEN);
		if (INFO) {
			for (i = 0; i < TX_MIC_KEY_LEN; i++)
				PRINT_INFO(CFG80211_DBG, "PairwiseTx[%d] = %x\n", i, pu8TxMic[i]);
		}
	}

	msg.body.key_info.attr.wpa.key_len = u8KeyLen;
	msg.body.key_info.attr.wpa.mac_addr = mac_addr;
	msg.body.key_info.attr.wpa.mode = u8Ciphermode;
	msg.drv = hif_drv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));

	if (s32Error)
		PRINT_ER("Error in sending message queue:  PTK Key\n");

	down(&hif_drv->hSemTestKeyBlock);

	return s32Error;
}

s32 host_int_add_rx_gtk(struct host_if_drv *hif_drv, const u8 *pu8RxGtk,
			u8 u8GtkKeylen,	u8 u8KeyIdx,
			u32 u32KeyRSClen, const u8 *KeyRSC,
			const u8 *pu8RxMic, const u8 *pu8TxMic,
			u8 mode, u8 u8Ciphermode)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	u8 u8KeyLen = u8GtkKeylen;

	if (!hif_drv) {
		s32Error = -EFAULT;
		PRINT_ER("driver is null\n");
		return s32Error;
	}
	memset(&msg, 0, sizeof(struct host_if_msg));


	if (pu8RxMic != NULL)
		u8KeyLen += RX_MIC_KEY_LEN;
	if (pu8TxMic != NULL)
		u8KeyLen += TX_MIC_KEY_LEN;
	if (KeyRSC != NULL) {
		msg.body.key_info.attr.wpa.seq = kmalloc(u32KeyRSClen, GFP_KERNEL);
		memcpy(msg.body.key_info.attr.wpa.seq, KeyRSC, u32KeyRSClen);
	}


	msg.id = HOST_IF_MSG_KEY;
	msg.body.key_info.type = WPARxGtk;
	msg.drv = hif_drv;

	if (mode == AP_MODE) {
		msg.body.key_info.action = ADDKEY_AP;
		msg.body.key_info.attr.wpa.mode = u8Ciphermode;
	}
	if (mode == STATION_MODE)
		msg.body.key_info.action = ADDKEY;

	msg.body.key_info.attr.wpa.key = kmalloc(u8KeyLen, GFP_KERNEL);
	memcpy(msg.body.key_info.attr.wpa.key, pu8RxGtk, u8GtkKeylen);

	if (pu8RxMic != NULL) {
		memcpy(msg.body.key_info.attr.wpa.key + 16, pu8RxMic, RX_MIC_KEY_LEN);
	}
	if (pu8TxMic != NULL) {
		memcpy(msg.body.key_info.attr.wpa.key + 24, pu8TxMic, TX_MIC_KEY_LEN);
	}

	msg.body.key_info.attr.wpa.index = u8KeyIdx;
	msg.body.key_info.attr.wpa.key_len = u8KeyLen;
	msg.body.key_info.attr.wpa.seq_len = u32KeyRSClen;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Error in sending message queue:  RX GTK\n");

	down(&hif_drv->hSemTestKeyBlock);

	return s32Error;
}

s32 host_int_set_pmkid_info(struct host_if_drv *hif_drv, struct host_if_pmkid_attr *pu8PmkidInfoArray)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	u32 i;


	if (!hif_drv) {
		s32Error = -EFAULT;
		PRINT_ER("driver is null\n");
		return s32Error;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_KEY;
	msg.body.key_info.type = PMKSA;
	msg.body.key_info.action = ADDKEY;
	msg.drv = hif_drv;

	for (i = 0; i < pu8PmkidInfoArray->numpmkid; i++) {
		memcpy(msg.body.key_info.attr.pmkid.pmkidlist[i].bssid, &pu8PmkidInfoArray->pmkidlist[i].bssid,
			    ETH_ALEN);
		memcpy(msg.body.key_info.attr.pmkid.pmkidlist[i].pmkid, &pu8PmkidInfoArray->pmkidlist[i].pmkid,
			    PMKID_LEN);
	}

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER(" Error in sending messagequeue: PMKID Info\n");

	return s32Error;
}

s32 host_int_get_pmkid_info(struct host_if_drv *hif_drv,
			    u8 *pu8PmkidInfoArray,
			    u32 u32PmkidInfoLen)
{
	struct wid strWID;

	strWID.id = (u16)WID_PMKID_INFO;
	strWID.type = WID_STR;
	strWID.size = u32PmkidInfoLen;
	strWID.val = pu8PmkidInfoArray;

	return 0;
}

s32 host_int_set_RSNAConfigPSKPassPhrase(struct host_if_drv *hif_drv,
					 u8 *pu8PassPhrase,
					 u8 u8Psklength)
{
	struct wid strWID;

	if ((u8Psklength > 7) && (u8Psklength < 65)) {
		strWID.id = (u16)WID_11I_PSK;
		strWID.type = WID_STR;
		strWID.val = pu8PassPhrase;
		strWID.size = u8Psklength;
	}

	return 0;
}

s32 host_int_get_MacAddress(struct host_if_drv *hif_drv, u8 *pu8MacAddress)
{
	s32 s32Error = 0;
	struct host_if_msg msg;

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_GET_MAC_ADDRESS;
	msg.body.get_mac_info.u8MacAddress = pu8MacAddress;
	msg.drv = hif_drv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("Failed to send get mac address\n");
		return -EFAULT;
	}

	down(&hWaitResponse);
	return s32Error;
}

s32 host_int_set_MacAddress(struct host_if_drv *hif_drv, u8 *pu8MacAddress)
{
	s32 s32Error = 0;
	struct host_if_msg msg;

	PRINT_D(GENERIC_DBG, "mac addr = %x:%x:%x\n", pu8MacAddress[0], pu8MacAddress[1], pu8MacAddress[2]);

	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_SET_MAC_ADDRESS;
	memcpy(msg.body.set_mac_info.u8MacAddress, pu8MacAddress, ETH_ALEN);
	msg.drv = hif_drv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Failed to send message queue: Set mac address\n");

	return s32Error;

}

s32 host_int_get_RSNAConfigPSKPassPhrase(struct host_if_drv *hif_drv,
					 u8 *pu8PassPhrase, u8 u8Psklength)
{
	struct wid strWID;

	strWID.id = (u16)WID_11I_PSK;
	strWID.type = WID_STR;
	strWID.size = u8Psklength;
	strWID.val = pu8PassPhrase;

	return 0;
}

s32 host_int_set_start_scan_req(struct host_if_drv *hif_drv, u8 scanSource)
{
	struct wid strWID;

	strWID.id = (u16)WID_START_SCAN_REQ;
	strWID.type = WID_CHAR;
	strWID.val = (s8 *)&scanSource;
	strWID.size = sizeof(char);

	return 0;
}

s32 host_int_get_start_scan_req(struct host_if_drv *hif_drv, u8 *pu8ScanSource)
{
	struct wid strWID;

	strWID.id = (u16)WID_START_SCAN_REQ;
	strWID.type = WID_CHAR;
	strWID.val = (s8 *)pu8ScanSource;
	strWID.size = sizeof(char);

	return 0;
}

s32 host_int_set_join_req(struct host_if_drv *hif_drv, u8 *pu8bssid,
			  const u8 *pu8ssid, size_t ssidLen,
			  const u8 *pu8IEs, size_t IEsLen,
			  wilc_connect_result pfConnectResult, void *pvUserArg,
			  u8 u8security, enum AUTHTYPE tenuAuth_type,
			  u8 u8channel, void *pJoinParams)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	enum scan_conn_timer enuScanConnTimer;

	if (!hif_drv || pfConnectResult == NULL) {
		s32Error = -EFAULT;
		PRINT_ER("Driver is null\n");
		return s32Error;
	}

	if (!hif_drv) {
		PRINT_ER("Driver is null\n");
		return -EFAULT;
	}

	if (pJoinParams == NULL) {
		PRINT_ER("Unable to Join - JoinParams is NULL\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_CONNECT;

	msg.body.con_info.security = u8security;
	msg.body.con_info.auth_type = tenuAuth_type;
	msg.body.con_info.ch = u8channel;
	msg.body.con_info.result = pfConnectResult;
	msg.body.con_info.arg = pvUserArg;
	msg.body.con_info.params = pJoinParams;
	msg.drv = hif_drv ;

	if (pu8bssid != NULL) {
		msg.body.con_info.bssid = kmalloc(6, GFP_KERNEL);
		memcpy(msg.body.con_info.bssid, pu8bssid, 6);
	}

	if (pu8ssid != NULL) {
		msg.body.con_info.ssid_len = ssidLen;
		msg.body.con_info.ssid = kmalloc(ssidLen, GFP_KERNEL);
		memcpy(msg.body.con_info.ssid, pu8ssid, ssidLen);
	}

	if (pu8IEs != NULL) {
		msg.body.con_info.ies_len = IEsLen;
		msg.body.con_info.ies = kmalloc(IEsLen, GFP_KERNEL);
		memcpy(msg.body.con_info.ies, pu8IEs, IEsLen);
	}
	if (hif_drv->enuHostIFstate < HOST_IF_CONNECTING)
		hif_drv->enuHostIFstate = HOST_IF_CONNECTING;
	else
		PRINT_D(GENERIC_DBG, "Don't set state to 'connecting' as state is %d\n", hif_drv->enuHostIFstate);

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("Failed to send message queue: Set join request\n");
		return -EFAULT;
	}

	enuScanConnTimer = CONNECT_TIMER;
	hif_drv->hConnectTimer.data = (unsigned long)hif_drv;
	mod_timer(&hif_drv->hConnectTimer,
		  jiffies + msecs_to_jiffies(HOST_IF_CONNECT_TIMEOUT));

	return s32Error;
}

s32 host_int_flush_join_req(struct host_if_drv *hif_drv)
{
	s32 s32Error = 0;
	struct host_if_msg msg;

	if (!gu8FlushedJoinReq)	{
		s32Error = -EFAULT;
		return s32Error;
	}


	if (!hif_drv) {
		s32Error = -EFAULT;
		PRINT_ER("Driver is null\n");
		return s32Error;
	}

	msg.id = HOST_IF_MSG_FLUSH_CONNECT;
	msg.drv = hif_drv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("Failed to send message queue: Flush join request\n");
		return -EFAULT;
	}

	return s32Error;
}

s32 host_int_disconnect(struct host_if_drv *hif_drv, u16 u16ReasonCode)
{
	s32 s32Error = 0;
	struct host_if_msg msg;

	if (!hif_drv) {
		PRINT_ER("Driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_DISCONNECT;
	msg.drv = hif_drv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Failed to send message queue: disconnect\n");

	down(&hif_drv->hSemTestDisconnectBlock);

	return s32Error;
}

s32 host_int_disconnect_station(struct host_if_drv *hif_drv, u8 assoc_id)
{
	struct wid strWID;

	strWID.id = (u16)WID_DISCONNECT;
	strWID.type = WID_CHAR;
	strWID.val = (s8 *)&assoc_id;
	strWID.size = sizeof(char);

	return 0;
}

s32 host_int_get_assoc_req_info(struct host_if_drv *hif_drv, u8 *pu8AssocReqInfo,
					u32 u32AssocReqInfoLen)
{
	struct wid strWID;

	strWID.id = (u16)WID_ASSOC_REQ_INFO;
	strWID.type = WID_STR;
	strWID.val = pu8AssocReqInfo;
	strWID.size = u32AssocReqInfoLen;

	return 0;
}

s32 host_int_get_assoc_res_info(struct host_if_drv *hif_drv, u8 *pu8AssocRespInfo,
					u32 u32MaxAssocRespInfoLen, u32 *pu32RcvdAssocRespInfoLen)
{
	s32 s32Error = 0;
	struct wid strWID;

	if (!hif_drv) {
		PRINT_ER("Driver is null\n");
		return -EFAULT;
	}

	strWID.id = (u16)WID_ASSOC_RES_INFO;
	strWID.type = WID_STR;
	strWID.val = pu8AssocRespInfo;
	strWID.size = u32MaxAssocRespInfoLen;

	s32Error = send_config_pkt(GET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));
	if (s32Error) {
		*pu32RcvdAssocRespInfoLen = 0;
		PRINT_ER("Failed to send association response config packet\n");
		return -EINVAL;
	} else {
		*pu32RcvdAssocRespInfoLen = strWID.size;
	}

	return s32Error;
}

s32 host_int_get_rx_power_level(struct host_if_drv *hif_drv, u8 *pu8RxPowerLevel,
					u32 u32RxPowerLevelLen)
{
	struct wid strWID;

	strWID.id = (u16)WID_RX_POWER_LEVEL;
	strWID.type = WID_STR;
	strWID.val = pu8RxPowerLevel;
	strWID.size = u32RxPowerLevelLen;

	return 0;
}

int host_int_set_mac_chnl_num(struct host_if_drv *hif_drv, u8 channel)
{
	int result;
	struct host_if_msg msg;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_SET_CHANNEL;
	msg.body.channel_info.set_ch = channel;
	msg.drv = hif_drv;

	result = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (result) {
		PRINT_ER("wilc mq send fail\n");
		return -EINVAL;
	}

	return 0;
}

int host_int_wait_msg_queue_idle(void)
{
	int result = 0;

	struct host_if_msg msg;
	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_Q_IDLE;
	result = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (result) {
		PRINT_ER("wilc mq send fail\n");
		result = -EINVAL;
	}

	down(&hWaitResponse);

	return result;
}

int host_int_set_wfi_drv_handler(struct host_if_drv *hif_drv)
{
	int result = 0;

	struct host_if_msg msg;
	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_SET_WFIDRV_HANDLER;
	msg.body.drv.u32Address = get_id_from_handler(hif_drv);
	msg.drv = hif_drv;

	result = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (result) {
		PRINT_ER("wilc mq send fail\n");
		result = -EINVAL;
	}

	return result;
}

int host_int_set_operation_mode(struct host_if_drv *hif_drv, u32 mode)
{
	int result = 0;

	struct host_if_msg msg;
	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_SET_OPERATION_MODE;
	msg.body.mode.u32Mode = mode;
	msg.drv = hif_drv;

	result = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (result) {
		PRINT_ER("wilc mq send fail\n");
		result = -EINVAL;
	}

	return result;
}

s32 host_int_get_host_chnl_num(struct host_if_drv *hif_drv, u8 *pu8ChNo)
{
	s32 s32Error = 0;
	struct host_if_msg msg;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_GET_CHNL;
	msg.drv = hif_drv;

	s32Error =	wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc mq send fail\n");
	down(&hif_drv->hSemGetCHNL);

	*pu8ChNo = gu8Chnl;

	return s32Error;


}

s32 host_int_get_inactive_time(struct host_if_drv *hif_drv,
			       const u8 *mac, u32 *pu32InactiveTime)
{
	s32 s32Error = 0;
	struct host_if_msg msg;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));


	memcpy(msg.body.mac_info.mac,
		    mac, ETH_ALEN);

	msg.id = HOST_IF_MSG_GET_INACTIVETIME;
	msg.drv = hif_drv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Failed to send get host channel param's message queue ");

	down(&hif_drv->hSemInactiveTime);

	*pu32InactiveTime = gu32InactiveTime;

	return s32Error;
}

s32 host_int_test_get_int_wid(struct host_if_drv *hif_drv, u32 *pu32TestMemAddr)
{

	s32 s32Error = 0;
	struct wid strWID;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	strWID.id = (u16)WID_MEMORY_ADDRESS;
	strWID.type = WID_INT;
	strWID.val = (s8 *)pu32TestMemAddr;
	strWID.size = sizeof(u32);

	s32Error = send_config_pkt(GET_CFG, &strWID, 1,
				   get_id_from_handler(hif_drv));

	if (s32Error) {
		PRINT_ER("Failed to get wid value\n");
		return -EINVAL;
	} else {
		PRINT_D(HOSTINF_DBG, "Successfully got wid value\n");

	}

	return s32Error;
}

s32 host_int_get_rssi(struct host_if_drv *hif_drv, s8 *ps8Rssi)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_GET_RSSI;
	msg.drv = hif_drv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("Failed to send get host channel param's message queue ");
		return -EFAULT;
	}

	down(&hif_drv->hSemGetRSSI);


	if (ps8Rssi == NULL) {
		PRINT_ER("RSS pointer value is null");
		return -EFAULT;
	}


	*ps8Rssi = gs8Rssi;


	return s32Error;
}

s32 host_int_get_link_speed(struct host_if_drv *hif_drv, s8 *ps8lnkspd)
{
	struct host_if_msg msg;
	s32 s32Error = 0;
	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_GET_LINKSPEED;
	msg.drv = hif_drv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("Failed to send GET_LINKSPEED to message queue ");
		return -EFAULT;
	}

	down(&hif_drv->hSemGetLINKSPEED);


	if (ps8lnkspd == NULL) {
		PRINT_ER("LINKSPEED pointer value is null");
		return -EFAULT;
	}


	*ps8lnkspd = gs8lnkspd;


	return s32Error;
}

s32 host_int_get_statistics(struct host_if_drv *hif_drv, struct rf_info *pstrStatistics)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_GET_STATISTICS;
	msg.body.data = (char *)pstrStatistics;
	msg.drv = hif_drv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("Failed to send get host channel param's message queue ");
		return -EFAULT;
	}

	down(&hWaitResponse);
	return s32Error;
}

s32 host_int_scan(struct host_if_drv *hif_drv, u8 u8ScanSource,
		  u8 u8ScanType, u8 *pu8ChnlFreqList,
		  u8 u8ChnlListLen, const u8 *pu8IEs,
		  size_t IEsLen, wilc_scan_result ScanResult,
		  void *pvUserArg, struct hidden_network *pstrHiddenNetwork)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	enum scan_conn_timer enuScanConnTimer;

	if (!hif_drv || ScanResult == NULL) {
		PRINT_ER("hif_drv or ScanResult = NULL\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_SCAN;

	if (pstrHiddenNetwork != NULL) {
		msg.body.scan_info.hidden_network.pstrHiddenNetworkInfo = pstrHiddenNetwork->pstrHiddenNetworkInfo;
		msg.body.scan_info.hidden_network.u8ssidnum = pstrHiddenNetwork->u8ssidnum;

	} else
		PRINT_D(HOSTINF_DBG, "pstrHiddenNetwork IS EQUAL TO NULL\n");

	msg.drv = hif_drv;
	msg.body.scan_info.src = u8ScanSource;
	msg.body.scan_info.type = u8ScanType;
	msg.body.scan_info.result = ScanResult;
	msg.body.scan_info.arg = pvUserArg;

	msg.body.scan_info.ch_list_len = u8ChnlListLen;
	msg.body.scan_info.ch_freq_list = kmalloc(u8ChnlListLen, GFP_KERNEL);
	memcpy(msg.body.scan_info.ch_freq_list, pu8ChnlFreqList, u8ChnlListLen);

	msg.body.scan_info.ies_len = IEsLen;
	msg.body.scan_info.ies = kmalloc(IEsLen, GFP_KERNEL);
	memcpy(msg.body.scan_info.ies, pu8IEs, IEsLen);

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("Error in sending message queue\n");
		return -EINVAL;
	}

	enuScanConnTimer = SCAN_TIMER;
	PRINT_D(HOSTINF_DBG, ">> Starting the SCAN timer\n");
	hif_drv->hScanTimer.data = (unsigned long)hif_drv;
	mod_timer(&hif_drv->hScanTimer,
		  jiffies + msecs_to_jiffies(HOST_IF_SCAN_TIMEOUT));

	return s32Error;

}

s32 hif_set_cfg(struct host_if_drv *hif_drv,
		struct cfg_param_val *pstrCfgParamVal)
{

	s32 s32Error = 0;
	struct host_if_msg msg;


	if (!hif_drv) {
		PRINT_ER("hif_drv NULL\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_CFG_PARAMS;
	msg.body.cfg_info.cfg_attr_info = *pstrCfgParamVal;
	msg.drv = hif_drv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));

	return s32Error;

}

s32 hif_get_cfg(struct host_if_drv *hif_drv, u16 u16WID, u16 *pu16WID_Value)
{
	s32 s32Error = 0;

	down(&hif_drv->gtOsCfgValuesSem);

	if (!hif_drv) {
		PRINT_ER("hif_drv NULL\n");
		return -EFAULT;
	}
	PRINT_D(HOSTINF_DBG, "Getting configuration parameters\n");
	switch (u16WID)	{

	case WID_BSS_TYPE:
		*pu16WID_Value = (u16)hif_drv->strCfgValues.bss_type;
		break;

	case WID_AUTH_TYPE:
		*pu16WID_Value = (u16)hif_drv->strCfgValues.auth_type;
		break;

	case WID_AUTH_TIMEOUT:
		*pu16WID_Value = hif_drv->strCfgValues.auth_timeout;
		break;

	case WID_POWER_MANAGEMENT:
		*pu16WID_Value = (u16)hif_drv->strCfgValues.power_mgmt_mode;
		break;

	case WID_SHORT_RETRY_LIMIT:
		*pu16WID_Value =       hif_drv->strCfgValues.short_retry_limit;
		break;

	case WID_LONG_RETRY_LIMIT:
		*pu16WID_Value = hif_drv->strCfgValues.long_retry_limit;
		break;

	case WID_FRAG_THRESHOLD:
		*pu16WID_Value = hif_drv->strCfgValues.frag_threshold;
		break;

	case WID_RTS_THRESHOLD:
		*pu16WID_Value = hif_drv->strCfgValues.rts_threshold;
		break;

	case WID_PREAMBLE:
		*pu16WID_Value = (u16)hif_drv->strCfgValues.preamble_type;
		break;

	case WID_SHORT_SLOT_ALLOWED:
		*pu16WID_Value = (u16) hif_drv->strCfgValues.short_slot_allowed;
		break;

	case WID_11N_TXOP_PROT_DISABLE:
		*pu16WID_Value = (u16)hif_drv->strCfgValues.txop_prot_disabled;
		break;

	case WID_BEACON_INTERVAL:
		*pu16WID_Value = hif_drv->strCfgValues.beacon_interval;
		break;

	case WID_DTIM_PERIOD:
		*pu16WID_Value = (u16)hif_drv->strCfgValues.dtim_period;
		break;

	case WID_SITE_SURVEY:
		*pu16WID_Value = (u16)hif_drv->strCfgValues.site_survey_enabled;
		break;

	case WID_SITE_SURVEY_SCAN_TIME:
		*pu16WID_Value = hif_drv->strCfgValues.site_survey_scan_time;
		break;

	case WID_ACTIVE_SCAN_TIME:
		*pu16WID_Value = hif_drv->strCfgValues.active_scan_time;
		break;

	case WID_PASSIVE_SCAN_TIME:
		*pu16WID_Value = hif_drv->strCfgValues.passive_scan_time;
		break;

	case WID_CURRENT_TX_RATE:
		*pu16WID_Value = hif_drv->strCfgValues.curr_tx_rate;
		break;

	default:
		break;
	}

	up(&hif_drv->gtOsCfgValuesSem);

	return s32Error;

}

void host_int_send_join_leave_info_to_host
	(u16 assocId, u8 *stationAddr, bool joining)
{
}

static void GetPeriodicRSSI(unsigned long arg)
{
	struct host_if_drv *hif_drv = (struct host_if_drv *)arg;

	if (!hif_drv)	{
		PRINT_ER("Driver handler is NULL\n");
		return;
	}

	if (hif_drv->enuHostIFstate == HOST_IF_CONNECTED) {
		s32 s32Error = 0;
		struct host_if_msg msg;

		memset(&msg, 0, sizeof(struct host_if_msg));

		msg.id = HOST_IF_MSG_GET_RSSI;
		msg.drv = hif_drv;

		s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
		if (s32Error) {
			PRINT_ER("Failed to send get host channel param's message queue ");
			return;
		}
	}
	g_hPeriodicRSSI.data = (unsigned long)hif_drv;
	mod_timer(&g_hPeriodicRSSI, jiffies + msecs_to_jiffies(5000));
}


void host_int_send_network_info_to_host
	(u8 *macStartAddress, u16 u16RxFrameLen, s8 s8Rssi)
{
}

static u32 clients_count;

s32 host_int_init(struct host_if_drv **hif_drv_handler)
{
	s32 result = 0;
	struct host_if_drv *hif_drv;
	int err;

	PRINT_D(HOSTINF_DBG, "Initializing host interface for client %d\n", clients_count + 1);

	gbScanWhileConnected = false;

	sema_init(&hWaitResponse, 0);

	hif_drv  = kzalloc(sizeof(struct host_if_drv), GFP_KERNEL);
	if (!hif_drv) {
		result = -ENOMEM;
		goto _fail_;
	}
	*hif_drv_handler = hif_drv;
	err = add_handler_in_list(hif_drv);
	if (err) {
		result = -EFAULT;
		goto _fail_timer_2;
	}

	g_obtainingIP = false;

	PRINT_D(HOSTINF_DBG, "Global handle pointer value=%p\n", hif_drv);
	if (clients_count == 0)	{
		sema_init(&hSemHostIFthrdEnd, 0);
		sema_init(&hSemDeinitDrvHandle, 0);
		sema_init(&hSemHostIntDeinit, 1);
	}

	sema_init(&hif_drv->hSemTestKeyBlock, 0);
	sema_init(&hif_drv->hSemTestDisconnectBlock, 0);
	sema_init(&hif_drv->hSemGetRSSI, 0);
	sema_init(&hif_drv->hSemGetLINKSPEED, 0);
	sema_init(&hif_drv->hSemGetCHNL, 0);
	sema_init(&hif_drv->hSemInactiveTime, 0);

	PRINT_D(HOSTINF_DBG, "INIT: CLIENT COUNT %d\n", clients_count);

	if (clients_count == 0)	{
		result = wilc_mq_create(&gMsgQHostIF);

		if (result < 0) {
			PRINT_ER("Failed to creat MQ\n");
			goto _fail_;
		}
		HostIFthreadHandler = kthread_run(hostIFthread, NULL, "WILC_kthread");
		if (IS_ERR(HostIFthreadHandler)) {
			PRINT_ER("Failed to creat Thread\n");
			result = -EFAULT;
			goto _fail_mq_;
		}
		setup_timer(&g_hPeriodicRSSI, GetPeriodicRSSI,
			    (unsigned long)hif_drv);
		mod_timer(&g_hPeriodicRSSI, jiffies + msecs_to_jiffies(5000));
	}

	setup_timer(&hif_drv->hScanTimer, TimerCB_Scan, 0);

	setup_timer(&hif_drv->hConnectTimer, TimerCB_Connect, 0);

	setup_timer(&hif_drv->hRemainOnChannel, ListenTimerCB, 0);

	sema_init(&(hif_drv->gtOsCfgValuesSem), 1);
	down(&hif_drv->gtOsCfgValuesSem);

	hif_drv->enuHostIFstate = HOST_IF_IDLE;
	hif_drv->strCfgValues.site_survey_enabled = SITE_SURVEY_OFF;
	hif_drv->strCfgValues.scan_source = DEFAULT_SCAN;
	hif_drv->strCfgValues.active_scan_time = ACTIVE_SCAN_TIME;
	hif_drv->strCfgValues.passive_scan_time = PASSIVE_SCAN_TIME;
	hif_drv->strCfgValues.curr_tx_rate = AUTORATE;

	hif_drv->u64P2p_MgmtTimeout = 0;

	PRINT_INFO(HOSTINF_DBG, "Initialization values, Site survey value: %d\n Scan source: %d\n Active scan time: %d\n Passive scan time: %d\nCurrent tx Rate = %d\n",

		   hif_drv->strCfgValues.site_survey_enabled, hif_drv->strCfgValues.scan_source,
		   hif_drv->strCfgValues.active_scan_time, hif_drv->strCfgValues.passive_scan_time,
		   hif_drv->strCfgValues.curr_tx_rate);

	up(&hif_drv->gtOsCfgValuesSem);

	clients_count++;

	return result;

_fail_timer_2:
	up(&hif_drv->gtOsCfgValuesSem);
	del_timer_sync(&hif_drv->hConnectTimer);
	del_timer_sync(&hif_drv->hScanTimer);
	kthread_stop(HostIFthreadHandler);
_fail_mq_:
	wilc_mq_destroy(&gMsgQHostIF);
_fail_:
	return result;
}

s32 host_int_deinit(struct host_if_drv *hif_drv)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	int ret;

	if (!hif_drv)	{
		PRINT_ER("hif_drv = NULL\n");
		return 0;
	}

	down(&hSemHostIntDeinit);

	terminated_handle = hif_drv;
	PRINT_D(HOSTINF_DBG, "De-initializing host interface for client %d\n", clients_count);

	if (del_timer_sync(&hif_drv->hScanTimer)) {
		PRINT_D(HOSTINF_DBG, ">> Scan timer is active\n");
	}

	if (del_timer_sync(&hif_drv->hConnectTimer)) {
		PRINT_D(HOSTINF_DBG, ">> Connect timer is active\n");
	}


	if (del_timer_sync(&g_hPeriodicRSSI)) {
		PRINT_D(HOSTINF_DBG, ">> Connect timer is active\n");
	}

	del_timer_sync(&hif_drv->hRemainOnChannel);

	host_int_set_wfi_drv_handler(NULL);
	down(&hSemDeinitDrvHandle);

	if (hif_drv->strWILC_UsrScanReq.pfUserScanResult) {
		hif_drv->strWILC_UsrScanReq.pfUserScanResult(SCAN_EVENT_ABORTED, NULL,
								hif_drv->strWILC_UsrScanReq.u32UserScanPvoid, NULL);

		hif_drv->strWILC_UsrScanReq.pfUserScanResult = NULL;
	}

	hif_drv->enuHostIFstate = HOST_IF_IDLE;

	gbScanWhileConnected = false;

	memset(&msg, 0, sizeof(struct host_if_msg));

	if (clients_count == 1)	{
		if (del_timer_sync(&g_hPeriodicRSSI)) {
			PRINT_D(HOSTINF_DBG, ">> Connect timer is active\n");
		}
		msg.id = HOST_IF_MSG_EXIT;
		msg.drv = hif_drv;


		s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
		if (s32Error != 0)
			PRINT_ER("Error in sending deinit's message queue message function: Error(%d)\n", s32Error);

		down(&hSemHostIFthrdEnd);

		wilc_mq_destroy(&gMsgQHostIF);
	}

	down(&(hif_drv->gtOsCfgValuesSem));

	ret = remove_handler_in_list(hif_drv);
	if (ret)
		s32Error = -ENOENT;

	kfree(hif_drv);

	clients_count--;
	terminated_handle = NULL;
	up(&hSemHostIntDeinit);
	return s32Error;
}

void NetworkInfoReceived(u8 *pu8Buffer, u32 u32Length)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	int id;
	struct host_if_drv *hif_drv = NULL;

	id = ((pu8Buffer[u32Length - 4]) | (pu8Buffer[u32Length - 3] << 8) | (pu8Buffer[u32Length - 2] << 16) | (pu8Buffer[u32Length - 1] << 24));
	hif_drv = get_handler_from_id(id);




	if (!hif_drv || hif_drv == terminated_handle)	{
		PRINT_ER("NetworkInfo received but driver not init[%p]\n", hif_drv);
		return;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_RCVD_NTWRK_INFO;
	msg.drv = hif_drv;

	msg.body.net_info.u32Length = u32Length;
	msg.body.net_info.pu8Buffer = kmalloc(u32Length, GFP_KERNEL);
	memcpy(msg.body.net_info.pu8Buffer,
		    pu8Buffer, u32Length);

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Error in sending network info message queue message parameters: Error(%d)\n", s32Error);
}

void GnrlAsyncInfoReceived(u8 *pu8Buffer, u32 u32Length)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	int id;
	struct host_if_drv *hif_drv = NULL;

	down(&hSemHostIntDeinit);

	id = ((pu8Buffer[u32Length - 4]) | (pu8Buffer[u32Length - 3] << 8) | (pu8Buffer[u32Length - 2] << 16) | (pu8Buffer[u32Length - 1] << 24));
	hif_drv = get_handler_from_id(id);
	PRINT_D(HOSTINF_DBG, "General asynchronous info packet received\n");


	if (!hif_drv || hif_drv == terminated_handle) {
		PRINT_D(HOSTINF_DBG, "Wifi driver handler is equal to NULL\n");
		up(&hSemHostIntDeinit);
		return;
	}

	if (!hif_drv->strWILC_UsrConnReq.pfUserConnectResult) {
		PRINT_ER("Received mac status is not needed when there is no current Connect Reques\n");
		up(&hSemHostIntDeinit);
		return;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));


	msg.id = HOST_IF_MSG_RCVD_GNRL_ASYNC_INFO;
	msg.drv = hif_drv;

	msg.body.async_info.len = u32Length;
	msg.body.async_info.buffer = kmalloc(u32Length, GFP_KERNEL);
	memcpy(msg.body.async_info.buffer, pu8Buffer, u32Length);

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Error in sending message queue asynchronous message info: Error(%d)\n", s32Error);

	up(&hSemHostIntDeinit);
}

void host_int_ScanCompleteReceived(u8 *pu8Buffer, u32 u32Length)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	int id;
	struct host_if_drv *hif_drv = NULL;

	id = ((pu8Buffer[u32Length - 4]) | (pu8Buffer[u32Length - 3] << 8) | (pu8Buffer[u32Length - 2] << 16) | (pu8Buffer[u32Length - 1] << 24));
	hif_drv = get_handler_from_id(id);


	PRINT_D(GENERIC_DBG, "Scan notification received %p\n", hif_drv);

	if (!hif_drv || hif_drv == terminated_handle)
		return;

	if (hif_drv->strWILC_UsrScanReq.pfUserScanResult) {
		memset(&msg, 0, sizeof(struct host_if_msg));

		msg.id = HOST_IF_MSG_RCVD_SCAN_COMPLETE;
		msg.drv = hif_drv;

		s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
		if (s32Error)
			PRINT_ER("Error in sending message queue scan complete parameters: Error(%d)\n", s32Error);
	}


	return;

}

s32 host_int_remain_on_channel(struct host_if_drv *hif_drv, u32 u32SessionID,
			       u32 u32duration, u16 chan,
			       wilc_remain_on_chan_expired RemainOnChanExpired,
			       wilc_remain_on_chan_ready RemainOnChanReady,
			       void *pvUserArg)
{
	s32 s32Error = 0;
	struct host_if_msg msg;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_REMAIN_ON_CHAN;
	msg.body.remain_on_ch.u16Channel = chan;
	msg.body.remain_on_ch.pRemainOnChanExpired = RemainOnChanExpired;
	msg.body.remain_on_ch.pRemainOnChanReady = RemainOnChanReady;
	msg.body.remain_on_ch.pVoid = pvUserArg;
	msg.body.remain_on_ch.u32duration = u32duration;
	msg.body.remain_on_ch.u32ListenSessionID = u32SessionID;
	msg.drv = hif_drv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc mq send fail\n");

	return s32Error;
}

s32 host_int_ListenStateExpired(struct host_if_drv *hif_drv, u32 u32SessionID)
{
	s32 s32Error = 0;
	struct host_if_msg msg;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	del_timer(&hif_drv->hRemainOnChannel);

	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_LISTEN_TIMER_FIRED;
	msg.drv = hif_drv;
	msg.body.remain_on_ch.u32ListenSessionID = u32SessionID;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc mq send fail\n");

	return s32Error;
}

s32 host_int_frame_register(struct host_if_drv *hif_drv, u16 u16FrameType, bool bReg)
{
	s32 s32Error = 0;
	struct host_if_msg msg;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_REGISTER_FRAME;
	switch (u16FrameType) {
	case ACTION:
		PRINT_D(HOSTINF_DBG, "ACTION\n");
		msg.body.reg_frame.u8Regid = ACTION_FRM_IDX;
		break;

	case PROBE_REQ:
		PRINT_D(HOSTINF_DBG, "PROBE REQ\n");
		msg.body.reg_frame.u8Regid = PROBE_REQ_IDX;
		break;

	default:
		PRINT_D(HOSTINF_DBG, "Not valid frame type\n");
		break;
	}
	msg.body.reg_frame.u16FrameType = u16FrameType;
	msg.body.reg_frame.bReg = bReg;
	msg.drv = hif_drv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc mq send fail\n");

	return s32Error;


}

s32 host_int_add_beacon(struct host_if_drv *hif_drv, u32 u32Interval,
			u32 u32DTIMPeriod, u32 u32HeadLen, u8 *pu8Head,
			u32 u32TailLen, u8 *pu8Tail)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	struct beacon_attr *pstrSetBeaconParam = &msg.body.beacon_info;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	PRINT_D(HOSTINF_DBG, "Setting adding beacon message queue params\n");

	msg.id = HOST_IF_MSG_ADD_BEACON;
	msg.drv = hif_drv;
	pstrSetBeaconParam->interval = u32Interval;
	pstrSetBeaconParam->dtim_period = u32DTIMPeriod;
	pstrSetBeaconParam->head_len = u32HeadLen;
	pstrSetBeaconParam->head = kmalloc(u32HeadLen, GFP_KERNEL);
	if (pstrSetBeaconParam->head == NULL) {
		s32Error = -ENOMEM;
		goto ERRORHANDLER;
	}
	memcpy(pstrSetBeaconParam->head, pu8Head, u32HeadLen);
	pstrSetBeaconParam->tail_len = u32TailLen;

	if (u32TailLen > 0) {
		pstrSetBeaconParam->tail = kmalloc(u32TailLen, GFP_KERNEL);
		if (pstrSetBeaconParam->tail == NULL) {
			s32Error = -ENOMEM;
			goto ERRORHANDLER;
		}
		memcpy(pstrSetBeaconParam->tail, pu8Tail, u32TailLen);
	} else {
		pstrSetBeaconParam->tail = NULL;
	}

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc mq send fail\n");

ERRORHANDLER:
	if (s32Error) {
		if (pstrSetBeaconParam->head != NULL)
			kfree(pstrSetBeaconParam->head);

		if (pstrSetBeaconParam->tail != NULL)
			kfree(pstrSetBeaconParam->tail);
	}

	return s32Error;

}

s32 host_int_del_beacon(struct host_if_drv *hif_drv)
{
	s32 s32Error = 0;
	struct host_if_msg msg;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	msg.id = HOST_IF_MSG_DEL_BEACON;
	msg.drv = hif_drv;
	PRINT_D(HOSTINF_DBG, "Setting deleting beacon message queue params\n");

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");

	return s32Error;
}

s32 host_int_add_station(struct host_if_drv *hif_drv,
			 struct add_sta_param *pstrStaParams)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	struct add_sta_param *pstrAddStationMsg = &msg.body.add_sta_info;


	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	PRINT_D(HOSTINF_DBG, "Setting adding station message queue params\n");

	msg.id = HOST_IF_MSG_ADD_STATION;
	msg.drv = hif_drv;

	memcpy(pstrAddStationMsg, pstrStaParams, sizeof(struct add_sta_param));
	if (pstrAddStationMsg->u8NumRates > 0) {
		u8 *rates = kmalloc(pstrAddStationMsg->u8NumRates, GFP_KERNEL);

		if (!rates)
			return -ENOMEM;

		memcpy(rates, pstrStaParams->pu8Rates, pstrAddStationMsg->u8NumRates);
		pstrAddStationMsg->pu8Rates = rates;
	}


	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");
	return s32Error;
}

s32 host_int_del_station(struct host_if_drv *hif_drv, const u8 *pu8MacAddr)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	struct del_sta *pstrDelStationMsg = &msg.body.del_sta_info;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	PRINT_D(HOSTINF_DBG, "Setting deleting station message queue params\n");

	msg.id = HOST_IF_MSG_DEL_STATION;
	msg.drv = hif_drv;

	if (pu8MacAddr == NULL)
		memset(pstrDelStationMsg->au8MacAddr, 255, ETH_ALEN);
	else
		memcpy(pstrDelStationMsg->au8MacAddr, pu8MacAddr, ETH_ALEN);

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");
	return s32Error;
}

s32 host_int_del_allstation(struct host_if_drv *hif_drv,
			    u8 pu8MacAddr[][ETH_ALEN])
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	struct del_all_sta *pstrDelAllStationMsg = &msg.body.del_all_sta_info;
	u8 au8Zero_Buff[ETH_ALEN] = {0};
	u32 i;
	u8 u8AssocNumb = 0;


	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	PRINT_D(HOSTINF_DBG, "Setting deauthenticating station message queue params\n");

	msg.id = HOST_IF_MSG_DEL_ALL_STA;
	msg.drv = hif_drv;

	for (i = 0; i < MAX_NUM_STA; i++) {
		if (memcmp(pu8MacAddr[i], au8Zero_Buff, ETH_ALEN)) {
			memcpy(pstrDelAllStationMsg->au8Sta_DelAllSta[i], pu8MacAddr[i], ETH_ALEN);
			PRINT_D(CFG80211_DBG, "BSSID = %x%x%x%x%x%x\n", pstrDelAllStationMsg->au8Sta_DelAllSta[i][0], pstrDelAllStationMsg->au8Sta_DelAllSta[i][1], pstrDelAllStationMsg->au8Sta_DelAllSta[i][2], pstrDelAllStationMsg->au8Sta_DelAllSta[i][3], pstrDelAllStationMsg->au8Sta_DelAllSta[i][4],
				pstrDelAllStationMsg->au8Sta_DelAllSta[i][5]);
			u8AssocNumb++;
		}
	}
	if (!u8AssocNumb) {
		PRINT_D(CFG80211_DBG, "NO ASSOCIATED STAS\n");
		return s32Error;
	}

	pstrDelAllStationMsg->u8Num_AssocSta = u8AssocNumb;
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));


	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");

	down(&hWaitResponse);

	return s32Error;

}

s32 host_int_edit_station(struct host_if_drv *hif_drv,
			  struct add_sta_param *pstrStaParams)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	struct add_sta_param *pstrAddStationMsg = &msg.body.add_sta_info;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	PRINT_D(HOSTINF_DBG, "Setting editing station message queue params\n");

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_EDIT_STATION;
	msg.drv = hif_drv;

	memcpy(pstrAddStationMsg, pstrStaParams, sizeof(struct add_sta_param));
	if (pstrAddStationMsg->u8NumRates > 0) {
		u8 *rates = kmalloc(pstrAddStationMsg->u8NumRates, GFP_KERNEL);

		if (!rates)
			return -ENOMEM;

		memcpy(rates, pstrStaParams->pu8Rates, pstrAddStationMsg->u8NumRates);
		pstrAddStationMsg->pu8Rates = rates;
	}

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");

	return s32Error;
}

s32 host_int_set_power_mgmt(struct host_if_drv *hif_drv,
			    bool bIsEnabled,
			    u32 u32Timeout)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	struct power_mgmt_param *pstrPowerMgmtParam = &msg.body.pwr_mgmt_info;

	PRINT_INFO(HOSTINF_DBG, "\n\n>> Setting PS to %d <<\n\n", bIsEnabled);

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	PRINT_D(HOSTINF_DBG, "Setting Power management message queue params\n");

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_POWER_MGMT;
	msg.drv = hif_drv;

	pstrPowerMgmtParam->bIsEnabled = bIsEnabled;
	pstrPowerMgmtParam->u32Timeout = u32Timeout;


	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");
	return s32Error;
}

s32 host_int_setup_multicast_filter(struct host_if_drv *hif_drv,
				    bool bIsEnabled,
				    u32 u32count)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	struct set_multicast *pstrMulticastFilterParam = &msg.body.multicast_info;


	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	PRINT_D(HOSTINF_DBG, "Setting Multicast Filter params\n");

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_SET_MULTICAST_FILTER;
	msg.drv = hif_drv;

	pstrMulticastFilterParam->bIsEnabled = bIsEnabled;
	pstrMulticastFilterParam->u32count = u32count;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");
	return s32Error;
}

static void *host_int_ParseJoinBssParam(tstrNetworkInfo *ptstrNetworkInfo)
{
	struct join_bss_param *pNewJoinBssParam = NULL;
	u8 *pu8IEs;
	u16 u16IEsLen;
	u16 index = 0;
	u8 suppRatesNo = 0;
	u8 extSuppRatesNo;
	u16 jumpOffset;
	u8 pcipherCount;
	u8 authCount;
	u8 pcipherTotalCount = 0;
	u8 authTotalCount = 0;
	u8 i, j;

	pu8IEs = ptstrNetworkInfo->pu8IEs;
	u16IEsLen = ptstrNetworkInfo->u16IEsLen;

	pNewJoinBssParam = kmalloc(sizeof(struct join_bss_param), GFP_KERNEL);
	if (pNewJoinBssParam != NULL) {
		memset(pNewJoinBssParam, 0, sizeof(struct join_bss_param));
		pNewJoinBssParam->dtim_period = ptstrNetworkInfo->u8DtimPeriod;
		pNewJoinBssParam->beacon_period = ptstrNetworkInfo->u16BeaconPeriod;
		pNewJoinBssParam->cap_info = ptstrNetworkInfo->u16CapInfo;
		memcpy(pNewJoinBssParam->au8bssid, ptstrNetworkInfo->au8bssid, 6);
		memcpy((u8 *)pNewJoinBssParam->ssid, ptstrNetworkInfo->au8ssid, ptstrNetworkInfo->u8SsidLen + 1);
		pNewJoinBssParam->ssidLen = ptstrNetworkInfo->u8SsidLen;
		memset(pNewJoinBssParam->rsn_pcip_policy, 0xFF, 3);
		memset(pNewJoinBssParam->rsn_auth_policy, 0xFF, 3);

		while (index < u16IEsLen) {
			if (pu8IEs[index] == SUPP_RATES_IE) {
				suppRatesNo = pu8IEs[index + 1];
				pNewJoinBssParam->supp_rates[0] = suppRatesNo;
				index += 2;

				for (i = 0; i < suppRatesNo; i++) {
					pNewJoinBssParam->supp_rates[i + 1] = pu8IEs[index + i];
				}
				index += suppRatesNo;
				continue;
			} else if (pu8IEs[index] == EXT_SUPP_RATES_IE) {
				extSuppRatesNo = pu8IEs[index + 1];
				if (extSuppRatesNo > (MAX_RATES_SUPPORTED - suppRatesNo))
					pNewJoinBssParam->supp_rates[0] = MAX_RATES_SUPPORTED;
				else
					pNewJoinBssParam->supp_rates[0] += extSuppRatesNo;
				index += 2;
				for (i = 0; i < (pNewJoinBssParam->supp_rates[0] - suppRatesNo); i++) {
					pNewJoinBssParam->supp_rates[suppRatesNo + i + 1] = pu8IEs[index + i];
				}
				index += extSuppRatesNo;
				continue;
			} else if (pu8IEs[index] == HT_CAPABILITY_IE) {
				pNewJoinBssParam->ht_capable = true;
				index += pu8IEs[index + 1] + 2;
				continue;
			} else if ((pu8IEs[index] == WMM_IE) &&
				   (pu8IEs[index + 2] == 0x00) && (pu8IEs[index + 3] == 0x50) &&
				   (pu8IEs[index + 4] == 0xF2) &&
				   (pu8IEs[index + 5] == 0x02) &&
				   ((pu8IEs[index + 6] == 0x00) || (pu8IEs[index + 6] == 0x01)) &&
				   (pu8IEs[index + 7] == 0x01)) {
				pNewJoinBssParam->wmm_cap = true;

				if (pu8IEs[index + 8] & BIT(7))
					pNewJoinBssParam->uapsd_cap = true;
				index += pu8IEs[index + 1] + 2;
				continue;
			} else if ((pu8IEs[index] == P2P_IE) &&
				 (pu8IEs[index + 2] == 0x50) && (pu8IEs[index + 3] == 0x6f) &&
				 (pu8IEs[index + 4] == 0x9a) &&
				 (pu8IEs[index + 5] == 0x09) && (pu8IEs[index + 6] == 0x0c)) {
				u16 u16P2P_count;

				pNewJoinBssParam->tsf = ptstrNetworkInfo->u32Tsf;
				pNewJoinBssParam->u8NoaEnbaled = 1;
				pNewJoinBssParam->u8Index = pu8IEs[index + 9];

				if (pu8IEs[index + 10] & BIT(7)) {
					pNewJoinBssParam->u8OppEnable = 1;
					pNewJoinBssParam->u8CtWindow = pu8IEs[index + 10];
				} else
					pNewJoinBssParam->u8OppEnable = 0;

				PRINT_D(GENERIC_DBG, "P2P Dump\n");
				for (i = 0; i < pu8IEs[index + 7]; i++)
					PRINT_D(GENERIC_DBG, " %x\n", pu8IEs[index + 9 + i]);

				pNewJoinBssParam->u8Count = pu8IEs[index + 11];
				u16P2P_count = index + 12;

				memcpy(pNewJoinBssParam->au8Duration, pu8IEs + u16P2P_count, 4);
				u16P2P_count += 4;

				memcpy(pNewJoinBssParam->au8Interval, pu8IEs + u16P2P_count, 4);
				u16P2P_count += 4;

				memcpy(pNewJoinBssParam->au8StartTime, pu8IEs + u16P2P_count, 4);

				index += pu8IEs[index + 1] + 2;
				continue;

			} else if ((pu8IEs[index] == RSN_IE) ||
				 ((pu8IEs[index] == WPA_IE) && (pu8IEs[index + 2] == 0x00) &&
				  (pu8IEs[index + 3] == 0x50) && (pu8IEs[index + 4] == 0xF2) &&
				  (pu8IEs[index + 5] == 0x01)))	{
				u16 rsnIndex = index;

				if (pu8IEs[rsnIndex] == RSN_IE)	{
					pNewJoinBssParam->mode_802_11i = 2;
				} else {
					if (pNewJoinBssParam->mode_802_11i == 0)
						pNewJoinBssParam->mode_802_11i = 1;
					rsnIndex += 4;
				}

				rsnIndex += 7;
				pNewJoinBssParam->rsn_grp_policy = pu8IEs[rsnIndex];
				rsnIndex++;
				jumpOffset = pu8IEs[rsnIndex] * 4;
				pcipherCount = (pu8IEs[rsnIndex] > 3) ? 3 : pu8IEs[rsnIndex];
				rsnIndex += 2;

				for (i = pcipherTotalCount, j = 0; i < pcipherCount + pcipherTotalCount && i < 3; i++, j++) {
					pNewJoinBssParam->rsn_pcip_policy[i] = pu8IEs[rsnIndex + ((j + 1) * 4) - 1];
				}
				pcipherTotalCount += pcipherCount;
				rsnIndex += jumpOffset;

				jumpOffset = pu8IEs[rsnIndex] * 4;

				authCount = (pu8IEs[rsnIndex] > 3) ? 3 : pu8IEs[rsnIndex];
				rsnIndex += 2;

				for (i = authTotalCount, j = 0; i < authTotalCount + authCount; i++, j++) {
					pNewJoinBssParam->rsn_auth_policy[i] = pu8IEs[rsnIndex + ((j + 1) * 4) - 1];
				}
				authTotalCount += authCount;
				rsnIndex += jumpOffset;

				if (pu8IEs[index] == RSN_IE) {
					pNewJoinBssParam->rsn_cap[0] = pu8IEs[rsnIndex];
					pNewJoinBssParam->rsn_cap[1] = pu8IEs[rsnIndex + 1];
					rsnIndex += 2;
				}
				pNewJoinBssParam->rsn_found = true;
				index += pu8IEs[index + 1] + 2;
				continue;
			} else
				index += pu8IEs[index + 1] + 2;

		}


	}

	return (void *)pNewJoinBssParam;

}

void host_int_freeJoinParams(void *pJoinParams)
{
	if ((struct bss_param *)pJoinParams != NULL)
		kfree((struct bss_param *)pJoinParams);
	else
		PRINT_ER("Unable to FREE null pointer\n");
}

s32 host_int_delBASession(struct host_if_drv *hif_drv, char *pBSSID, char TID)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	struct ba_session_info *pBASessionInfo = &msg.body.session_info;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_DEL_BA_SESSION;

	memcpy(pBASessionInfo->au8Bssid, pBSSID, ETH_ALEN);
	pBASessionInfo->u8Ted = TID;
	msg.drv = hif_drv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");

	down(&hWaitResponse);

	return s32Error;
}

s32 host_int_del_All_Rx_BASession(struct host_if_drv *hif_drv,
				  char *pBSSID,
				  char TID)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	struct ba_session_info *pBASessionInfo = &msg.body.session_info;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_DEL_ALL_RX_BA_SESSIONS;

	memcpy(pBASessionInfo->au8Bssid, pBSSID, ETH_ALEN);
	pBASessionInfo->u8Ted = TID;
	msg.drv = hif_drv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");

	down(&hWaitResponse);

	return s32Error;
}

s32 host_int_setup_ipaddress(struct host_if_drv *hif_drv, u8 *u16ipadd, u8 idx)
{
	s32 s32Error = 0;
	struct host_if_msg msg;

	return 0;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_SET_IPADDRESS;

	msg.body.ip_info.au8IPAddr = u16ipadd;
	msg.drv = hif_drv;
	msg.body.ip_info.idx = idx;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");

	return s32Error;


}

s32 host_int_get_ipaddress(struct host_if_drv *hif_drv, u8 *u16ipadd, u8 idx)
{
	s32 s32Error = 0;
	struct host_if_msg msg;

	if (!hif_drv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_GET_IPADDRESS;

	msg.body.ip_info.au8IPAddr = u16ipadd;
	msg.drv = hif_drv;
	msg.body.ip_info.idx = idx;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");

	return s32Error;


}
