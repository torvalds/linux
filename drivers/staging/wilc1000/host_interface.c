#include <linux/slab.h>
#include <linux/time.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include "host_interface.h"
#include "coreconfigurator.h"
#include "wilc_wlan_if.h"
#include "wilc_msgqueue.h"

extern u8 connecting;

extern struct timer_list hDuringIpTimer;

extern u8 g_wilc_initialized;

/* Message types of the Host IF Message Queue*/
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

/*!
 *  @struct             cfg_param_attr
 *  @brief		Structure to hold Host IF CFG Params Attributes
 *  @details
 *  @todo
 *  @sa
 *  @author		Mai Daftedar
 *  @date		02 April 2012
 *  @version		1.0
 */
struct cfg_param_attr {
	tstrCfgParamVal pstrCfgParamVal;
};

/*!
 *  @struct             tstrHostIFwpaAttr
 *  @brief		Structure to hold Host IF Scan Attributes
 *  @details
 *  @todo
 *  @sa
 *  @author		Mai Daftedar
 *  @date		25 March 2012
 *  @version		1.0
 */
typedef struct _tstrHostIFwpaAttr {
	u8 *pu8key;
	const u8 *pu8macaddr;
	u8 *pu8seq;
	u8 u8seqlen;
	u8 u8keyidx;
	u8 u8Keylen;
	u8 u8Ciphermode;
} tstrHostIFwpaAttr;


/*!
 *  @struct             tstrHostIFwepAttr
 *  @brief		Structure to hold Host IF Scan Attributes
 *  @details
 *  @todo
 *  @sa
 *  @author		Mai Daftedar
 *  @date		25 March 2012
 *  @version		1.0
 */
typedef struct _tstrHostIFwepAttr {
	u8 *pu8WepKey;
	u8 u8WepKeylen;
	u8 u8Wepidx;
	u8 u8mode;
	AUTHTYPE_T tenuAuth_type;

} tstrHostIFwepAttr;

/*!
 *  @struct             tuniHostIFkeyAttr
 *  @brief		Structure to hold Host IF Scan Attributes
 *  @details
 *  @todo
 *  @sa
 *  @author		Mai Daftedar
 *  @date		25 March 2012
 *  @version		1.0
 */
typedef union _tuniHostIFkeyAttr {
	tstrHostIFwepAttr strHostIFwepAttr;
	tstrHostIFwpaAttr strHostIFwpaAttr;
	tstrHostIFpmkidAttr strHostIFpmkidAttr;
} tuniHostIFkeyAttr;

/*!
 *  @struct             key_attr
 *  @brief		Structure to hold Host IF Scan Attributes
 *  @details
 *  @todo
 *  @sa
 *  @author		Mai Daftedar
 *  @date		25 March 2012
 *  @version		1.0
 */
struct key_attr {
	tenuKeyType enuKeyType;
	u8 u8KeyAction;
	tuniHostIFkeyAttr uniHostIFkeyAttr;
};




/*!
 *  @struct             scan_attr
 *  @brief		Structure to hold Host IF Scan Attributes
 *  @details
 *  @todo
 *  @sa
 *  @author		Mostafa Abu Bakr
 *  @date		25 March 2012
 *  @version		1.0
 */
struct scan_attr {
	u8 u8ScanSource;
	u8 u8ScanType;
	u8 *pu8ChnlFreqList;
	u8 u8ChnlListLen;
	u8 *pu8IEs;
	size_t IEsLen;
	tWILCpfScanResult pfScanResult;
	void *pvUserArg;
	tstrHiddenNetwork strHiddenNetwork;
};

/*!
 *  @struct             connect_attr
 *  @brief		Structure to hold Host IF Connect Attributes
 *  @details
 *  @todo
 *  @sa
 *  @author		Mostafa Abu Bakr
 *  @date		25 March 2012
 *  @version		1.0
 */
struct connect_attr {
	u8 *pu8bssid;
	u8 *pu8ssid;
	size_t ssidLen;
	u8 *pu8IEs;
	size_t IEsLen;
	u8 u8security;
	tWILCpfConnectResult pfConnectResult;
	void *pvUserArg;
	AUTHTYPE_T tenuAuth_type;
	u8 u8channel;
	void *pJoinParams;
};

/*!
 *  @struct             rcvd_async_info
 *  @brief		Structure to hold Received General Asynchronous info
 *  @details
 *  @todo
 *  @sa
 *  @author		Mostafa Abu Bakr
 *  @date		25 March 2012
 *  @version		1.0
 */
struct rcvd_async_info {
	u8 *pu8Buffer;
	u32 u32Length;
};

/*!
 *  @struct		channel_attr
 *  @brief		Set Channel  message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Mai Daftedar
 *  @date		25 March 2012
 *  @version		1.0
 */
struct channel_attr {
	u8 u8SetChan;
};

/*!
 *  @struct             tstrScanComplete
 *  @brief			hold received Async. Scan Complete message body
 *  @details
 *  @todo
 *  @sa
 *  @author		zsalah
 *  @date		25 March 2012
 *  @version		1.0
 */
/*typedef struct _tstrScanComplete
 * {
 *      u8* pu8Buffer;
 *      u32 u32Length;
 * } tstrScanComplete;*/

/*!
 *  @struct             beacon_attr
 *  @brief		Set Beacon  message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Adham Abozaeid
 *  @date		10 July 2012
 *  @version		1.0
 */
struct beacon_attr {
	u32 u32Interval;                        /*!< Beacon Interval. Period between two successive beacons on air  */
	u32 u32DTIMPeriod;              /*!< DTIM Period. Indicates how many Beacon frames
											*                              (including the current frame) appear before the next DTIM		*/
	u32 u32HeadLen;                         /*!< Length of the head buffer in bytes		*/
	u8 *pu8Head;                    /*!< Pointer to the beacon's head buffer. Beacon's head	is the part
											*              from the beacon's start till the TIM element, NOT including the TIM		*/
	u32 u32TailLen;                         /*!< Length of the tail buffer in bytes	*/
	u8 *pu8Tail;                    /*!< Pointer to the beacon's tail buffer. Beacon's tail	starts just
											*                              after the TIM inormation element	*/
};

/*!
 *  @struct             set_multicast
 *  @brief		set Multicast filter Address
 *  @details
 *  @todo
 *  @sa
 *  @author		Abdelrahman Sobhy
 *  @date		30 August 2013
 *  @version		1.0 Description
 */

struct set_multicast {
	bool bIsEnabled;
	u32 u32count;
};

/*!
 *  @struct             del_all_sta
 *  @brief		Deauth station message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Mai Daftedar
 *  @date		09 April 2014
 *  @version		1.0 Description
 */
struct del_all_sta {
	u8 au8Sta_DelAllSta[MAX_NUM_STA][ETH_ALEN];
	u8 u8Num_AssocSta;
};

/*!
 *  @struct             del_sta
 *  @brief		Delete station message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Adham Abozaeid
 *  @date		15 July 2012
 *  @version		1.0 Description
 */
struct del_sta {
	u8 au8MacAddr[ETH_ALEN];
};

/*!
 *  @struct     power_mgmt_param
 *  @brief		Power management message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Adham Abozaeid
 *  @date		24 November 2012
 *  @version		1.0
 */
struct power_mgmt_param {

	bool bIsEnabled;
	u32 u32Timeout;
};

/*!
 *  @struct             set_ip_addr
 *  @brief		set IP Address message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Abdelrahman Sobhy
 *  @date		30 August 2013
 *  @version		1.0 Description
 */
struct set_ip_addr {
	u8 *au8IPAddr;
	u8 idx;
};

/*!
 *  @struct     sta_inactive_t
 *  @brief		Get station message body
 *  @details
 *  @todo
 *  @sa
 *  @author	    Mai Daftedar
 *  @date		16 April 2013
 *  @version		1.0
 */
struct sta_inactive_t {
	u8 mac[6];
};
/**/
/*!
 *  @union              message_body
 *  @brief		Message body for the Host Interface message_q
 *  @details
 *  @todo
 *  @sa
 *  @author		Mostafa Abu Bakr
 *  @date		25 March 2012
 *  @version		1.0
 */
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
	struct reg_frame strHostIfRegisterFrame;
	char *pUserData;
	struct del_all_sta strHostIFDelAllSta;
};

/*!
 *  @struct             struct host_if_msg
 *  @brief		Host Interface message
 *  @details
 *  @todo
 *  @sa
 *  @author		Mostafa Abu Bakr
 *  @date		25 March 2012
 *  @version		1.0
 */
struct host_if_msg {
	u16 id;                                           /*!< Message ID */
	union message_body body;             /*!< Message body */
	tstrWILC_WFIDrv *drvHandler;
};

typedef struct _tstrWidJoinReqExt {
	char SSID[MAX_SSID_LEN];
	u8 u8channel;
	u8 BSSID[6];
} tstrWidJoinReqExt;

/*Struct containg joinParam of each AP*/
typedef struct _tstrJoinBssParam {
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
	struct _tstrJoinParam *nextJoinBss;
	u32 tsf;
	u8 u8NoaEnbaled;
	u8 u8OppEnable;
	u8 u8CtWindow;
	u8 u8Count;
	u8 u8Index;
	u8 au8Duration[4];
	u8 au8Interval[4];
	u8 au8StartTime[4];
} tstrJoinBssParam;
/*a linked list table containing needed join parameters entries for each AP found in most recent scan*/
typedef struct _tstrBssTable {
	u8 u8noBssEntries;
	tstrJoinBssParam *head;
	tstrJoinBssParam *tail;
} tstrBssTable;

typedef enum {
	SCAN_TIMER = 0,
	CONNECT_TIMER	= 1,
	SCAN_CONNECT_TIMER_FORCE_32BIT = 0xFFFFFFFF
} tenuScanConnTimer;

/*****************************************************************************/
/*																			 */
/*							Global Variabls	                                                                 */
/*																			 */
/*****************************************************************************/
/* Zero is not used, because a zero ID means termination */
static tstrWILC_WFIDrv *wfidrv_list[NUM_CONCURRENT_IFC + 1];
tstrWILC_WFIDrv *terminated_handle;
tstrWILC_WFIDrv *gWFiDrvHandle;
bool g_obtainingIP = false;
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

bool gbScanWhileConnected = false;

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
tstrWILC_WFIDrv *gu8FlushedJoinReqDrvHandler;
#define REAL_JOIN_REQ 0
#define FLUSHED_JOIN_REQ 1
#define FLUSHED_BYTE_POS 79     /* Position the byte indicating flushing in the flushed request */

static void *host_int_ParseJoinBssParam(tstrNetworkInfo *ptstrNetworkInfo);

extern void chip_sleep_manually(u32 u32SleepTime);
extern int linux_wlan_get_num_conn_ifcs(void);

static int add_handler_in_list(tstrWILC_WFIDrv *handler)
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

static int remove_handler_in_list(tstrWILC_WFIDrv *handler)
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

static int get_id_from_handler(tstrWILC_WFIDrv *handler)
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

static tstrWILC_WFIDrv *get_handler_from_id(int id)
{
	if (id <= 0 || id >= ARRAY_SIZE(wfidrv_list))
		return NULL;
	return wfidrv_list[id];
}

/**
 *  @brief Handle_SetChannel
 *  @details    Sending config packet to firmware to set channel
 *  @param[in]   struct channel_attr *pstrHostIFSetChan
 *  @return     Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_SetChannel(tstrWILC_WFIDrv *drvHandler,
			     struct channel_attr *pstrHostIFSetChan)
{

	s32 s32Error = 0;
	tstrWID	strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_CURRENT_CHANNEL;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = (char *)&(pstrHostIFSetChan->u8SetChan);
	strWID.s32ValueSize = sizeof(char);

	PRINT_D(HOSTINF_DBG, "Setting channel\n");
	/*Sending Cfg*/
	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error) {
		PRINT_ER("Failed to set channel\n");
		return -EINVAL;
	}

	return s32Error;
}
/**
 *  @brief Handle_SetWfiDrvHandler
 *  @details    Sending config packet to firmware to set driver handler
 *  @param[in]   void * drvHandler,
 *		 struct drv_handler *pstrHostIfSetDrvHandler
 *  @return     Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_SetWfiDrvHandler(tstrWILC_WFIDrv *drvHandler,
				   struct drv_handler *pstrHostIfSetDrvHandler)
{

	s32 s32Error = 0;
	tstrWID	strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = drvHandler;


	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_SET_DRV_HANDLER;
	strWID.enuWIDtype = WID_INT;
	strWID.ps8WidVal = (s8 *)&(pstrHostIfSetDrvHandler->u32Address);
	strWID.s32ValueSize = sizeof(u32);

	/*Sending Cfg*/

	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   pstrHostIfSetDrvHandler->u32Address);

	if (pstrWFIDrv == NULL)
		up(&hSemDeinitDrvHandle);


	if (s32Error) {
		PRINT_ER("Failed to set driver handler\n");
		return -EINVAL;
	}

	return s32Error;
}

/**
 *  @brief Handle_SetWfiAPDrvHandler
 *  @details    Sending config packet to firmware to set driver handler
 *  @param[in]   void * drvHandler,tstrHostIfSetDrvHandler* pstrHostIfSetDrvHandler
 *  @return     Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_SetOperationMode(tstrWILC_WFIDrv *drvHandler,
				   struct op_mode *pstrHostIfSetOperationMode)
{

	s32 s32Error = 0;
	tstrWID	strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;


	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_SET_OPERATION_MODE;
	strWID.enuWIDtype = WID_INT;
	strWID.ps8WidVal = (s8 *)&(pstrHostIfSetOperationMode->u32Mode);
	strWID.s32ValueSize = sizeof(u32);

	/*Sending Cfg*/
	PRINT_INFO(HOSTINF_DBG, "pstrWFIDrv= %p\n", pstrWFIDrv);

	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));


	if ((pstrHostIfSetOperationMode->u32Mode) == IDLE_MODE)
		up(&hSemDeinitDrvHandle);


	if (s32Error) {
		PRINT_ER("Failed to set driver handler\n");
		return -EINVAL;
	}

	return s32Error;
}

/**
 *  @brief host_int_set_IPAddress
 *  @details       Setting IP address params in message queue
 *  @param[in]    WILC_WFIDrvHandle hWFIDrv, u8* pu8IPAddr
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
s32 Handle_set_IPAddress(tstrWILC_WFIDrv *drvHandler, u8 *pu8IPAddr, u8 idx)
{

	s32 s32Error = 0;
	tstrWID strWID;
	char firmwareIPAddress[4] = {0};
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	if (pu8IPAddr[0] < 192)
		pu8IPAddr[0] = 0;

	PRINT_INFO(HOSTINF_DBG, "Indx = %d, Handling set  IP = %pI4\n", idx, pu8IPAddr);

	memcpy(gs8SetIP[idx], pu8IPAddr, IP_ALEN);

	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_IP_ADDRESS;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = (u8 *)pu8IPAddr;
	strWID.s32ValueSize = IP_ALEN;

	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));


	host_int_get_ipaddress(drvHandler, firmwareIPAddress, idx);

	if (s32Error) {
		PRINT_ER("Failed to set IP address\n");
		return -EINVAL;
	}

	PRINT_INFO(HOSTINF_DBG, "IP address set\n");

	return s32Error;
}


/**
 *  @brief Handle_get_IPAddress
 *  @details       Setting IP address params in message queue
 *  @param[in]    WILC_WFIDrvHandle hWFIDrv, u8* pu8IPAddr
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
s32 Handle_get_IPAddress(tstrWILC_WFIDrv *drvHandler, u8 *pu8IPAddr, u8 idx)
{

	s32 s32Error = 0;
	tstrWID strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_IP_ADDRESS;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = kmalloc(IP_ALEN, GFP_KERNEL);
	strWID.s32ValueSize = IP_ALEN;

	s32Error = send_config_pkt(GET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));

	PRINT_INFO(HOSTINF_DBG, "%pI4\n", strWID.ps8WidVal);

	memcpy(gs8GetIP[idx], strWID.ps8WidVal, IP_ALEN);

	/*get the value by searching the local copy*/
	kfree(strWID.ps8WidVal);

	if (memcmp(gs8GetIP[idx], gs8SetIP[idx], IP_ALEN) != 0)
		host_int_setup_ipaddress(pstrWFIDrv, gs8SetIP[idx], idx);

	if (s32Error != 0) {
		PRINT_ER("Failed to get IP address\n");
		return -EINVAL;
	}

	PRINT_INFO(HOSTINF_DBG, "IP address retrieved:: u8IfIdx = %d\n", idx);
	PRINT_INFO(HOSTINF_DBG, "%pI4\n", gs8GetIP[idx]);
	PRINT_INFO(HOSTINF_DBG, "\n");

	return s32Error;
}


/**
 *  @brief Handle_SetMacAddress
 *  @details    Setting mac address
 *  @param[in]   void * drvHandler,tstrHostIfSetDrvHandler* pstrHostIfSetDrvHandler
 *  @return     Error code.
 *  @author	Amr Abdel-Moghny
 *  @date		November 2013
 *  @version	7.0
 */
static s32 Handle_SetMacAddress(tstrWILC_WFIDrv *drvHandler,
				struct set_mac_addr *pstrHostIfSetMacAddress)
{

	s32 s32Error = 0;
	tstrWID	strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;
	u8 *mac_buf = kmalloc(ETH_ALEN, GFP_KERNEL);

	if (mac_buf == NULL) {
		PRINT_ER("No buffer to send mac address\n");
		return -EFAULT;
	}
	memcpy(mac_buf, pstrHostIfSetMacAddress->u8MacAddress, ETH_ALEN);

	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_MAC_ADDR;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = mac_buf;
	strWID.s32ValueSize = ETH_ALEN;
	PRINT_D(GENERIC_DBG, "mac addr = :%pM\n", strWID.ps8WidVal);
	/*Sending Cfg*/
	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error) {
		PRINT_ER("Failed to set mac address\n");
		s32Error = -EFAULT;
	}

	kfree(mac_buf);
	return s32Error;
}


/**
 *  @brief Handle_GetMacAddress
 *  @details    Getting mac address
 *  @param[in]   void * drvHandler,tstrHostIfSetDrvHandler* pstrHostIfSetDrvHandler
 *  @return     Error code.
 *  @author	Amr Abdel-Moghny
 *  @date		JAN 2013
 *  @version	8.0
 */
static s32 Handle_GetMacAddress(tstrWILC_WFIDrv *drvHandler,
				struct get_mac_addr *pstrHostIfGetMacAddress)
{

	s32 s32Error = 0;
	tstrWID	strWID;

	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_MAC_ADDR;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = pstrHostIfGetMacAddress->u8MacAddress;
	strWID.s32ValueSize = ETH_ALEN;

	/*Sending Cfg*/
	s32Error = send_config_pkt(GET_CFG, &strWID, 1, false,
				   get_id_from_handler(drvHandler));
	if (s32Error) {
		PRINT_ER("Failed to get mac address\n");
		s32Error = -EFAULT;
	}
	up(&hWaitResponse);

	return s32Error;
}


/**
 *  @brief Handle_CfgParam
 *  @details    Sending config packet to firmware to set CFG params
 *  @param[in]   struct cfg_param_attr *strHostIFCfgParamAttr
 *  @return     Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_CfgParam(tstrWILC_WFIDrv *drvHandler,
			   struct cfg_param_attr *strHostIFCfgParamAttr)
{
	s32 s32Error = 0;
	tstrWID strWIDList[32];
	u8 u8WidCnt = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;


	down(&(pstrWFIDrv->gtOsCfgValuesSem));


	PRINT_D(HOSTINF_DBG, "Setting CFG params\n");

	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & BSS_TYPE) {
		/*----------------------------------------------------------*/
		/*Input Value:	INFRASTRUCTURE = 1,							*/
		/*				INDEPENDENT= 2,								*/
		/*				ANY_BSS= 3									*/
		/*----------------------------------------------------------*/
		/* validate input then copy>> need to check value 4 and 5 */
		if (strHostIFCfgParamAttr->pstrCfgParamVal.bss_type < 6) {
			strWIDList[u8WidCnt].u16WIDid = WID_BSS_TYPE;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.bss_type;
			strWIDList[u8WidCnt].enuWIDtype = WID_CHAR;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(char);
			pstrWFIDrv->strCfgValues.bss_type = (u8)strHostIFCfgParamAttr->pstrCfgParamVal.bss_type;
		} else {
			PRINT_ER("check value 6 over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & AUTH_TYPE) {
		/*------------------------------------------------------*/
		/*Input Values: OPEN_SYSTEM     = 0,					*/
		/*				SHARED_KEY      = 1,					*/
		/*				ANY             = 2						*/
		/*------------------------------------------------------*/
		/*validate Possible values*/
		if ((strHostIFCfgParamAttr->pstrCfgParamVal.auth_type) == 1 || (strHostIFCfgParamAttr->pstrCfgParamVal.auth_type) == 2 || (strHostIFCfgParamAttr->pstrCfgParamVal.auth_type) == 5) {
			strWIDList[u8WidCnt].u16WIDid = WID_AUTH_TYPE;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.auth_type;
			strWIDList[u8WidCnt].enuWIDtype = WID_CHAR;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(char);
			pstrWFIDrv->strCfgValues.auth_type = (u8)strHostIFCfgParamAttr->pstrCfgParamVal.auth_type;
		} else {
			PRINT_ER("Impossible value \n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & AUTHEN_TIMEOUT) {
		/* range is 1 to 65535. */
		if (strHostIFCfgParamAttr->pstrCfgParamVal.auth_timeout > 0 && strHostIFCfgParamAttr->pstrCfgParamVal.auth_timeout < 65536) {
			strWIDList[u8WidCnt].u16WIDid = WID_AUTH_TIMEOUT;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.auth_timeout;
			strWIDList[u8WidCnt].enuWIDtype = WID_SHORT;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(u16);
			pstrWFIDrv->strCfgValues.auth_timeout = strHostIFCfgParamAttr->pstrCfgParamVal.auth_timeout;
		} else {
			PRINT_ER("Range(1 ~ 65535) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & POWER_MANAGEMENT) {
		/*-----------------------------------------------------------*/
		/*Input Values:	NO_POWERSAVE     = 0,						*/
		/*				MIN_FAST_PS      = 1,						*/
		/*				MAX_FAST_PS      = 2,						*/
		/*				MIN_PSPOLL_PS    = 3,						*/
		/*				MAX_PSPOLL_PS    = 4						*/
		/*----------------------------------------------------------*/
		if (strHostIFCfgParamAttr->pstrCfgParamVal.power_mgmt_mode < 5) {
			strWIDList[u8WidCnt].u16WIDid = WID_POWER_MANAGEMENT;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.power_mgmt_mode;
			strWIDList[u8WidCnt].enuWIDtype = WID_CHAR;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(char);
			pstrWFIDrv->strCfgValues.power_mgmt_mode = (u8)strHostIFCfgParamAttr->pstrCfgParamVal.power_mgmt_mode;
		} else {
			PRINT_ER("Invalide power mode\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & RETRY_SHORT) {
		/* range from 1 to 256 */
		if ((strHostIFCfgParamAttr->pstrCfgParamVal.short_retry_limit > 0) && (strHostIFCfgParamAttr->pstrCfgParamVal.short_retry_limit < 256))	{
			strWIDList[u8WidCnt].u16WIDid = WID_SHORT_RETRY_LIMIT;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.short_retry_limit;
			strWIDList[u8WidCnt].enuWIDtype = WID_SHORT;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(u16);
			pstrWFIDrv->strCfgValues.short_retry_limit = strHostIFCfgParamAttr->pstrCfgParamVal.short_retry_limit;
		} else {
			PRINT_ER("Range(1~256) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & RETRY_LONG) {
		/* range from 1 to 256 */
		if ((strHostIFCfgParamAttr->pstrCfgParamVal.long_retry_limit > 0) && (strHostIFCfgParamAttr->pstrCfgParamVal.long_retry_limit < 256)) {
			strWIDList[u8WidCnt].u16WIDid = WID_LONG_RETRY_LIMIT;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.long_retry_limit;

			strWIDList[u8WidCnt].enuWIDtype = WID_SHORT;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(u16);
			pstrWFIDrv->strCfgValues.long_retry_limit = strHostIFCfgParamAttr->pstrCfgParamVal.long_retry_limit;
		} else {
			PRINT_ER("Range(1~256) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & FRAG_THRESHOLD) {

		if (strHostIFCfgParamAttr->pstrCfgParamVal.frag_threshold > 255 && strHostIFCfgParamAttr->pstrCfgParamVal.frag_threshold < 7937) {
			strWIDList[u8WidCnt].u16WIDid = WID_FRAG_THRESHOLD;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.frag_threshold;
			strWIDList[u8WidCnt].enuWIDtype = WID_SHORT;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(u16);
			pstrWFIDrv->strCfgValues.frag_threshold = strHostIFCfgParamAttr->pstrCfgParamVal.frag_threshold;
		} else {
			PRINT_ER("Threshold Range fail\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & RTS_THRESHOLD) {
		/* range 256 to 65535 */
		if (strHostIFCfgParamAttr->pstrCfgParamVal.rts_threshold > 255 && strHostIFCfgParamAttr->pstrCfgParamVal.rts_threshold < 65536)	{
			strWIDList[u8WidCnt].u16WIDid = WID_RTS_THRESHOLD;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.rts_threshold;
			strWIDList[u8WidCnt].enuWIDtype = WID_SHORT;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(u16);
			pstrWFIDrv->strCfgValues.rts_threshold = strHostIFCfgParamAttr->pstrCfgParamVal.rts_threshold;
		} else {
			PRINT_ER("Threshold Range fail\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & PREAMBLE) {
		/*-----------------------------------------------------*/
		/*Input Values: Short= 0,								*/
		/*				Long= 1,                                */
		/*				Auto= 2									*/
		/*------------------------------------------------------*/
		if (strHostIFCfgParamAttr->pstrCfgParamVal.preamble_type < 3) {
			strWIDList[u8WidCnt].u16WIDid = WID_PREAMBLE;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.preamble_type;
			strWIDList[u8WidCnt].enuWIDtype = WID_CHAR;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(char);
			pstrWFIDrv->strCfgValues.preamble_type = strHostIFCfgParamAttr->pstrCfgParamVal.preamble_type;
		} else {
			PRINT_ER("Preamle Range(0~2) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & SHORT_SLOT_ALLOWED) {
		if (strHostIFCfgParamAttr->pstrCfgParamVal.short_slot_allowed < 2) {
			strWIDList[u8WidCnt].u16WIDid = WID_SHORT_SLOT_ALLOWED;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.short_slot_allowed;
			strWIDList[u8WidCnt].enuWIDtype = WID_CHAR;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(char);
			pstrWFIDrv->strCfgValues.short_slot_allowed = (u8)strHostIFCfgParamAttr->pstrCfgParamVal.short_slot_allowed;
		} else {
			PRINT_ER("Short slot(2) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & TXOP_PROT_DISABLE) {
		/*Description:	used to Disable RTS-CTS protection for TXOP burst*/
		/*transmission when the acknowledgement policy is No-Ack or Block-Ack	*/
		/* this information is useful for external supplicant                                   */
		/*Input Values: 1 for enable and 0 for disable.							*/
		if (strHostIFCfgParamAttr->pstrCfgParamVal.txop_prot_disabled < 2) {
			strWIDList[u8WidCnt].u16WIDid = WID_11N_TXOP_PROT_DISABLE;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.txop_prot_disabled;
			strWIDList[u8WidCnt].enuWIDtype = WID_CHAR;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(char);
			pstrWFIDrv->strCfgValues.txop_prot_disabled = (u8)strHostIFCfgParamAttr->pstrCfgParamVal.txop_prot_disabled;
		} else {
			PRINT_ER("TXOP prot disable\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & BEACON_INTERVAL) {
		/* range is 1 to 65535. */
		if (strHostIFCfgParamAttr->pstrCfgParamVal.beacon_interval > 0 && strHostIFCfgParamAttr->pstrCfgParamVal.beacon_interval < 65536) {
			strWIDList[u8WidCnt].u16WIDid = WID_BEACON_INTERVAL;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.beacon_interval;
			strWIDList[u8WidCnt].enuWIDtype = WID_SHORT;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(u16);
			pstrWFIDrv->strCfgValues.beacon_interval = strHostIFCfgParamAttr->pstrCfgParamVal.beacon_interval;
		} else {
			PRINT_ER("Beacon interval(1~65535) fail\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & DTIM_PERIOD) {
		/* range is 1 to 255. */
		if (strHostIFCfgParamAttr->pstrCfgParamVal.dtim_period > 0 && strHostIFCfgParamAttr->pstrCfgParamVal.dtim_period < 256) {
			strWIDList[u8WidCnt].u16WIDid = WID_DTIM_PERIOD;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.dtim_period;
			strWIDList[u8WidCnt].enuWIDtype = WID_CHAR;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(char);
			pstrWFIDrv->strCfgValues.dtim_period = strHostIFCfgParamAttr->pstrCfgParamVal.dtim_period;
		} else {
			PRINT_ER("DTIM range(1~255) fail\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & SITE_SURVEY) {
		/*----------------------------------------------------------------------*/
		/*Input Values: SITE_SURVEY_1CH    = 0, i.e.: currently set channel		*/
		/*				SITE_SURVEY_ALL_CH = 1,									*/
		/*				SITE_SURVEY_OFF    = 2									*/
		/*----------------------------------------------------------------------*/
		if (strHostIFCfgParamAttr->pstrCfgParamVal.site_survey_enabled < 3) {
			strWIDList[u8WidCnt].u16WIDid = WID_SITE_SURVEY;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.site_survey_enabled;
			strWIDList[u8WidCnt].enuWIDtype = WID_CHAR;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(char);
			pstrWFIDrv->strCfgValues.site_survey_enabled = (u8)strHostIFCfgParamAttr->pstrCfgParamVal.site_survey_enabled;
		} else {
			PRINT_ER("Site survey disable\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & SITE_SURVEY_SCAN_TIME) {
		/* range is 1 to 65535. */
		if (strHostIFCfgParamAttr->pstrCfgParamVal.site_survey_scan_time > 0 && strHostIFCfgParamAttr->pstrCfgParamVal.site_survey_scan_time < 65536) {
			strWIDList[u8WidCnt].u16WIDid = WID_SITE_SURVEY_SCAN_TIME;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.site_survey_scan_time;
			strWIDList[u8WidCnt].enuWIDtype = WID_SHORT;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(u16);
			pstrWFIDrv->strCfgValues.site_survey_scan_time = strHostIFCfgParamAttr->pstrCfgParamVal.site_survey_scan_time;
		} else {
			PRINT_ER("Site survey scan time(1~65535) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & ACTIVE_SCANTIME) {
		/* range is 1 to 65535. */
		if (strHostIFCfgParamAttr->pstrCfgParamVal.active_scan_time > 0 && strHostIFCfgParamAttr->pstrCfgParamVal.active_scan_time < 65536) {
			strWIDList[u8WidCnt].u16WIDid = WID_ACTIVE_SCAN_TIME;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.active_scan_time;
			strWIDList[u8WidCnt].enuWIDtype = WID_SHORT;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(u16);
			pstrWFIDrv->strCfgValues.active_scan_time = strHostIFCfgParamAttr->pstrCfgParamVal.active_scan_time;
		} else {
			PRINT_ER("Active scan time(1~65535) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & PASSIVE_SCANTIME) {
		/* range is 1 to 65535. */
		if (strHostIFCfgParamAttr->pstrCfgParamVal.passive_scan_time > 0 && strHostIFCfgParamAttr->pstrCfgParamVal.passive_scan_time < 65536) {
			strWIDList[u8WidCnt].u16WIDid = WID_PASSIVE_SCAN_TIME;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&strHostIFCfgParamAttr->pstrCfgParamVal.passive_scan_time;
			strWIDList[u8WidCnt].enuWIDtype = WID_SHORT;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(u16);
			pstrWFIDrv->strCfgValues.passive_scan_time = strHostIFCfgParamAttr->pstrCfgParamVal.passive_scan_time;
		} else {
			PRINT_ER("Passive scan time(1~65535) over\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	if (strHostIFCfgParamAttr->pstrCfgParamVal.u32SetCfgFlag & CURRENT_TX_RATE) {
		CURRENT_TX_RATE_T curr_tx_rate = strHostIFCfgParamAttr->pstrCfgParamVal.curr_tx_rate;
		/*----------------------------------------------------------------------*/
		/*Rates:		1   2   5.5   11   6  9  12  18  24  36  48   54  Auto	*/
		/*InputValues:	1   2     3    4   5  6   7   8   9  10  11   12  0		*/
		/*----------------------------------------------------------------------*/
		/* validate rate */
		if (curr_tx_rate == AUTORATE || curr_tx_rate == MBPS_1
		    || curr_tx_rate == MBPS_2 || curr_tx_rate == MBPS_5_5
		    || curr_tx_rate == MBPS_11 || curr_tx_rate == MBPS_6
		    || curr_tx_rate == MBPS_9 || curr_tx_rate == MBPS_12
		    || curr_tx_rate == MBPS_18 || curr_tx_rate == MBPS_24
		    || curr_tx_rate == MBPS_36 || curr_tx_rate == MBPS_48 || curr_tx_rate == MBPS_54) {
			strWIDList[u8WidCnt].u16WIDid = WID_CURRENT_TX_RATE;
			strWIDList[u8WidCnt].ps8WidVal = (s8 *)&curr_tx_rate;
			strWIDList[u8WidCnt].enuWIDtype = WID_SHORT;
			strWIDList[u8WidCnt].s32ValueSize = sizeof(u16);
			pstrWFIDrv->strCfgValues.curr_tx_rate = (u8)curr_tx_rate;
		} else {
			PRINT_ER("out of TX rate\n");
			s32Error = -EINVAL;
			goto ERRORHANDLER;
		}
		u8WidCnt++;
	}
	s32Error = send_config_pkt(SET_CFG, strWIDList, u8WidCnt, false,
				   get_id_from_handler(pstrWFIDrv));

	if (s32Error)
		PRINT_ER("Error in setting CFG params\n");

ERRORHANDLER:
	up(&(pstrWFIDrv->gtOsCfgValuesSem));
	return s32Error;
}


/**
 *  @brief Handle_wait_msg_q_empty
 *  @details       this should be the last msg and then the msg Q becomes idle
 *  @param[in]    tstrHostIFscanAttr* pstrHostIFscanAttr
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_wait_msg_q_empty(void)
{
	s32 s32Error = 0;

	g_wilc_initialized = 0;
	up(&hWaitResponse);
	return s32Error;
}

/**
 *  @brief Handle_Scan
 *  @details       Sending config packet to firmware to set the scan params
 *  @param[in]    struct scan_attr *pstrHostIFscanAttr
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_Scan(tstrWILC_WFIDrv *drvHandler,
		       struct scan_attr *pstrHostIFscanAttr)
{
	s32 s32Error = 0;
	tstrWID strWIDList[5];
	u32 u32WidsCount = 0;
	u32 i;
	u8 *pu8Buffer;
	u8 valuesize = 0;
	u8 *pu8HdnNtwrksWidVal = NULL;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *) drvHandler;

	PRINT_D(HOSTINF_DBG, "Setting SCAN params\n");
	PRINT_D(HOSTINF_DBG, "Scanning: In [%d] state\n", pstrWFIDrv->enuHostIFstate);

	pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult = pstrHostIFscanAttr->pfScanResult;
	pstrWFIDrv->strWILC_UsrScanReq.u32UserScanPvoid = pstrHostIFscanAttr->pvUserArg;

	if ((pstrWFIDrv->enuHostIFstate >= HOST_IF_SCANNING) && (pstrWFIDrv->enuHostIFstate < HOST_IF_CONNECTED)) {
		/* here we either in HOST_IF_SCANNING, HOST_IF_WAITING_CONN_REQ or HOST_IF_WAITING_CONN_RESP */
		PRINT_D(GENERIC_DBG, "Don't scan we are already in [%d] state\n", pstrWFIDrv->enuHostIFstate);
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


	pstrWFIDrv->strWILC_UsrScanReq.u32RcvdChCount = 0;

	strWIDList[u32WidsCount].u16WIDid = (u16)WID_SSID_PROBE_REQ;
	strWIDList[u32WidsCount].enuWIDtype = WID_STR;

	for (i = 0; i < pstrHostIFscanAttr->strHiddenNetwork.u8ssidnum; i++)
		valuesize += ((pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo[i].u8ssidlen) + 1);
	pu8HdnNtwrksWidVal = kmalloc(valuesize + 1, GFP_KERNEL);
	strWIDList[u32WidsCount].ps8WidVal = pu8HdnNtwrksWidVal;
	if (strWIDList[u32WidsCount].ps8WidVal != NULL) {
		pu8Buffer = strWIDList[u32WidsCount].ps8WidVal;

		*pu8Buffer++ = pstrHostIFscanAttr->strHiddenNetwork.u8ssidnum;

		PRINT_D(HOSTINF_DBG, "In Handle_ProbeRequest number of ssid %d\n", pstrHostIFscanAttr->strHiddenNetwork.u8ssidnum);

		for (i = 0; i < pstrHostIFscanAttr->strHiddenNetwork.u8ssidnum; i++) {
			*pu8Buffer++ = pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo[i].u8ssidlen;
			memcpy(pu8Buffer, pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo[i].pu8ssid, pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo[i].u8ssidlen);
			pu8Buffer += pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo[i].u8ssidlen;
		}



		strWIDList[u32WidsCount].s32ValueSize =  (s32)(valuesize + 1);
		u32WidsCount++;
	}

	/*filling cfg param array*/

	/* if((pstrHostIFscanAttr->pu8IEs != NULL) && (pstrHostIFscanAttr->IEsLen != 0)) */
	{
		/* IEs to be inserted in Probe Request */
		strWIDList[u32WidsCount].u16WIDid = WID_INFO_ELEMENT_PROBE;
		strWIDList[u32WidsCount].enuWIDtype = WID_BIN_DATA;
		strWIDList[u32WidsCount].ps8WidVal = pstrHostIFscanAttr->pu8IEs;
		strWIDList[u32WidsCount].s32ValueSize = pstrHostIFscanAttr->IEsLen;
		u32WidsCount++;
	}

	/*Scan Type*/
	strWIDList[u32WidsCount].u16WIDid = WID_SCAN_TYPE;
	strWIDList[u32WidsCount].enuWIDtype = WID_CHAR;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(char);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&(pstrHostIFscanAttr->u8ScanType));
	u32WidsCount++;

	/*list of channels to be scanned*/
	strWIDList[u32WidsCount].u16WIDid = WID_SCAN_CHANNEL_LIST;
	strWIDList[u32WidsCount].enuWIDtype = WID_BIN_DATA;

	if (pstrHostIFscanAttr->pu8ChnlFreqList != NULL && pstrHostIFscanAttr->u8ChnlListLen > 0) {
		int i;

		for (i = 0; i < pstrHostIFscanAttr->u8ChnlListLen; i++)	{
			if (pstrHostIFscanAttr->pu8ChnlFreqList[i] > 0)
				pstrHostIFscanAttr->pu8ChnlFreqList[i] = pstrHostIFscanAttr->pu8ChnlFreqList[i] - 1;
		}
	}

	strWIDList[u32WidsCount].ps8WidVal = pstrHostIFscanAttr->pu8ChnlFreqList;
	strWIDList[u32WidsCount].s32ValueSize = pstrHostIFscanAttr->u8ChnlListLen;
	u32WidsCount++;

	/*Scan Request*/
	strWIDList[u32WidsCount].u16WIDid = WID_START_SCAN_REQ;
	strWIDList[u32WidsCount].enuWIDtype = WID_CHAR;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(char);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&(pstrHostIFscanAttr->u8ScanSource));
	u32WidsCount++;

	/*keep the state as is , no need to change it*/
	/* gWFiDrvHandle->enuHostIFstate = HOST_IF_SCANNING; */

	if (pstrWFIDrv->enuHostIFstate == HOST_IF_CONNECTED)
		gbScanWhileConnected = true;
	else if (pstrWFIDrv->enuHostIFstate == HOST_IF_IDLE)
		gbScanWhileConnected = false;

	s32Error = send_config_pkt(SET_CFG, strWIDList, u32WidsCount, false,
				   get_id_from_handler(pstrWFIDrv));

	if (s32Error)
		PRINT_ER("Failed to send scan paramters config packet\n");
	else
		PRINT_D(HOSTINF_DBG, "Successfully sent SCAN params config packet\n");

ERRORHANDLER:
	if (s32Error) {
		del_timer(&pstrWFIDrv->hScanTimer);
		/*if there is an ongoing scan request*/
		Handle_ScanDone(drvHandler, SCAN_EVENT_ABORTED);
	}

	/* Deallocate pstrHostIFscanAttr->u8ChnlListLen which was prevoisuly allocated by the sending thread */
	if (pstrHostIFscanAttr->pu8ChnlFreqList != NULL) {
		kfree(pstrHostIFscanAttr->pu8ChnlFreqList);
		pstrHostIFscanAttr->pu8ChnlFreqList = NULL;
	}

	/* Deallocate pstrHostIFscanAttr->pu8IEs which was previously allocated by the sending thread */
	if (pstrHostIFscanAttr->pu8IEs != NULL)	{
		kfree(pstrHostIFscanAttr->pu8IEs);
		pstrHostIFscanAttr->pu8IEs = NULL;
	}
	if (pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo != NULL)	{
		kfree(pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo);
		pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo = NULL;
	}

	/* Deallocate pstrHostIFscanAttr->u8ChnlListLen which was prevoisuly allocated by the sending thread */
	if (pstrHostIFscanAttr->pu8ChnlFreqList != NULL) {
		kfree(pstrHostIFscanAttr->pu8ChnlFreqList);
		pstrHostIFscanAttr->pu8ChnlFreqList = NULL;
	}

	if (pu8HdnNtwrksWidVal != NULL)
		kfree(pu8HdnNtwrksWidVal);

	return s32Error;
}

/**
 *  @brief Handle_ScanDone
 *  @details       Call scan notification callback function
 *  @param[in]    NONE
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_ScanDone(tstrWILC_WFIDrv *drvHandler, tenuScanEvent enuEvent)
{
	s32 s32Error = 0;

	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;


	u8 u8abort_running_scan;
	tstrWID strWID;


	PRINT_D(HOSTINF_DBG, "in Handle_ScanDone()\n");

	/*Ask FW to abort the running scan, if any*/
	if (enuEvent == SCAN_EVENT_ABORTED) {
		PRINT_D(GENERIC_DBG, "Abort running scan\n");
		u8abort_running_scan = 1;
		strWID.u16WIDid	= (u16)WID_ABORT_RUNNING_SCAN;
		strWID.enuWIDtype	= WID_CHAR;
		strWID.ps8WidVal = (s8 *)&u8abort_running_scan;
		strWID.s32ValueSize = sizeof(char);

		/*Sending Cfg*/
		s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
					   get_id_from_handler(pstrWFIDrv));
		if (s32Error) {
			PRINT_ER("Failed to set abort running scan\n");
			s32Error = -EFAULT;
		}
	}

	if (pstrWFIDrv == NULL)	{
		PRINT_ER("Driver handler is NULL\n");
		return s32Error;
	}

	/*if there is an ongoing scan request*/
	if (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult) {
		pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult(enuEvent, NULL,
								pstrWFIDrv->strWILC_UsrScanReq.u32UserScanPvoid, NULL);
		/*delete current scan request*/
		pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult = NULL;
	}

	return s32Error;
}

/**
 *  @brief Handle_Connect
 *  @details       Sending config packet to firmware to starting connection
 *  @param[in]    struct connect_attr *pstrHostIFconnectAttr
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
u8 u8ConnectedSSID[6] = {0};
static s32 Handle_Connect(tstrWILC_WFIDrv *drvHandler,
			  struct connect_attr *pstrHostIFconnectAttr)
{
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *) drvHandler;
	s32 s32Error = 0;
	tstrWID strWIDList[8];
	u32 u32WidsCount = 0, dummyval = 0;
	/* char passphrase[] = "12345678"; */
	u8 *pu8CurrByte = NULL;
	tstrJoinBssParam *ptstrJoinBssParam;

	PRINT_D(GENERIC_DBG, "Handling connect request\n");

	/* if we try to connect to an already connected AP then discard the request */

	if (memcmp(pstrHostIFconnectAttr->pu8bssid, u8ConnectedSSID, ETH_ALEN) == 0) {

		s32Error = 0;
		PRINT_ER("Trying to connect to an already connected AP, Discard connect request\n");
		return s32Error;
	}

	PRINT_INFO(HOSTINF_DBG, "Saving connection parameters in global structure\n");

	ptstrJoinBssParam = (tstrJoinBssParam *)pstrHostIFconnectAttr->pJoinParams;
	if (ptstrJoinBssParam == NULL) {
		PRINT_ER("Required BSSID not found\n");
		s32Error = -ENOENT;
		goto ERRORHANDLER;
	}

	if (pstrHostIFconnectAttr->pu8bssid != NULL) {
		pstrWFIDrv->strWILC_UsrConnReq.pu8bssid = kmalloc(6, GFP_KERNEL);
		memcpy(pstrWFIDrv->strWILC_UsrConnReq.pu8bssid, pstrHostIFconnectAttr->pu8bssid, 6);
	}

	pstrWFIDrv->strWILC_UsrConnReq.ssidLen = pstrHostIFconnectAttr->ssidLen;
	if (pstrHostIFconnectAttr->pu8ssid != NULL) {
		pstrWFIDrv->strWILC_UsrConnReq.pu8ssid = kmalloc(pstrHostIFconnectAttr->ssidLen + 1, GFP_KERNEL);
		memcpy(pstrWFIDrv->strWILC_UsrConnReq.pu8ssid, pstrHostIFconnectAttr->pu8ssid,
			    pstrHostIFconnectAttr->ssidLen);
		pstrWFIDrv->strWILC_UsrConnReq.pu8ssid[pstrHostIFconnectAttr->ssidLen] = '\0';
	}

	pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen = pstrHostIFconnectAttr->IEsLen;
	if (pstrHostIFconnectAttr->pu8IEs != NULL) {
		pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs = kmalloc(pstrHostIFconnectAttr->IEsLen, GFP_KERNEL);
		memcpy(pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs, pstrHostIFconnectAttr->pu8IEs,
			    pstrHostIFconnectAttr->IEsLen);
	}

	pstrWFIDrv->strWILC_UsrConnReq.u8security = pstrHostIFconnectAttr->u8security;
	pstrWFIDrv->strWILC_UsrConnReq.tenuAuth_type = pstrHostIFconnectAttr->tenuAuth_type;
	pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult = pstrHostIFconnectAttr->pfConnectResult;
	pstrWFIDrv->strWILC_UsrConnReq.u32UserConnectPvoid = pstrHostIFconnectAttr->pvUserArg;

	strWIDList[u32WidsCount].u16WIDid = WID_SUCCESS_FRAME_COUNT;
	strWIDList[u32WidsCount].enuWIDtype = WID_INT;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(u32);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&(dummyval));
	u32WidsCount++;

	strWIDList[u32WidsCount].u16WIDid = WID_RECEIVED_FRAGMENT_COUNT;
	strWIDList[u32WidsCount].enuWIDtype = WID_INT;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(u32);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&(dummyval));
	u32WidsCount++;

	strWIDList[u32WidsCount].u16WIDid = WID_FAILED_COUNT;
	strWIDList[u32WidsCount].enuWIDtype = WID_INT;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(u32);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&(dummyval));
	u32WidsCount++;

	/* if((gWFiDrvHandle->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) && */
	/* (gWFiDrvHandle->strWILC_UsrConnReq.ConnReqIEsLen != 0)) */
	{
		/* IEs to be inserted in Association Request */
		strWIDList[u32WidsCount].u16WIDid = WID_INFO_ELEMENT_ASSOCIATE;
		strWIDList[u32WidsCount].enuWIDtype = WID_BIN_DATA;
		strWIDList[u32WidsCount].ps8WidVal = pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs;
		strWIDList[u32WidsCount].s32ValueSize = pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen;
		u32WidsCount++;

		if (memcmp("DIRECT-", pstrHostIFconnectAttr->pu8ssid, 7)) {

			gu32FlushedInfoElemAsocSize = pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen;
			gu8FlushedInfoElemAsoc =  kmalloc(gu32FlushedInfoElemAsocSize, GFP_KERNEL);
			memcpy(gu8FlushedInfoElemAsoc, pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs,
			       gu32FlushedInfoElemAsocSize);
		}
	}
	strWIDList[u32WidsCount].u16WIDid = (u16)WID_11I_MODE;
	strWIDList[u32WidsCount].enuWIDtype = WID_CHAR;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(char);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&(pstrWFIDrv->strWILC_UsrConnReq.u8security));
	u32WidsCount++;

	if (memcmp("DIRECT-", pstrHostIFconnectAttr->pu8ssid, 7))
		gu8Flushed11iMode = pstrWFIDrv->strWILC_UsrConnReq.u8security;

	PRINT_INFO(HOSTINF_DBG, "Encrypt Mode = %x\n", pstrWFIDrv->strWILC_UsrConnReq.u8security);


	strWIDList[u32WidsCount].u16WIDid = (u16)WID_AUTH_TYPE;
	strWIDList[u32WidsCount].enuWIDtype = WID_CHAR;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(char);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&pstrWFIDrv->strWILC_UsrConnReq.tenuAuth_type);
	u32WidsCount++;

	if (memcmp("DIRECT-", pstrHostIFconnectAttr->pu8ssid, 7))
		gu8FlushedAuthType = (u8)pstrWFIDrv->strWILC_UsrConnReq.tenuAuth_type;

	PRINT_INFO(HOSTINF_DBG, "Authentication Type = %x\n", pstrWFIDrv->strWILC_UsrConnReq.tenuAuth_type);
	/*
	 * strWIDList[u32WidsCount].u16WIDid = (u16)WID_11I_PSK;
	 * strWIDList[u32WidsCount].enuWIDtype = WID_STR;
	 * strWIDList[u32WidsCount].s32ValueSize = sizeof(passphrase);
	 * strWIDList[u32WidsCount].ps8WidVal = (s8*)(passphrase);
	 * u32WidsCount++;
	 */

	PRINT_D(HOSTINF_DBG, "Connecting to network of SSID %s on channel %d\n",
		pstrWFIDrv->strWILC_UsrConnReq.pu8ssid, pstrHostIFconnectAttr->u8channel);

	strWIDList[u32WidsCount].u16WIDid = (u16)WID_JOIN_REQ_EXTENDED;
	strWIDList[u32WidsCount].enuWIDtype = WID_STR;

	/*Sending NoA attributes during connection*/
	strWIDList[u32WidsCount].s32ValueSize = 112; /* 79; */
	strWIDList[u32WidsCount].ps8WidVal = kmalloc(strWIDList[u32WidsCount].s32ValueSize, GFP_KERNEL);

	if (memcmp("DIRECT-", pstrHostIFconnectAttr->pu8ssid, 7)) {
		gu32FlushedJoinReqSize = strWIDList[u32WidsCount].s32ValueSize;
		gu8FlushedJoinReq = kmalloc(gu32FlushedJoinReqSize, GFP_KERNEL);
	}
	if (strWIDList[u32WidsCount].ps8WidVal == NULL) {
		s32Error = -EFAULT;
		goto ERRORHANDLER;
	}

	pu8CurrByte = strWIDList[u32WidsCount].ps8WidVal;


	if (pstrHostIFconnectAttr->pu8ssid != NULL) {
		memcpy(pu8CurrByte, pstrHostIFconnectAttr->pu8ssid, pstrHostIFconnectAttr->ssidLen);
		pu8CurrByte[pstrHostIFconnectAttr->ssidLen] = '\0';
	}
	pu8CurrByte += MAX_SSID_LEN;

	/* BSS type*/
	*(pu8CurrByte++) = INFRASTRUCTURE;
	/* Channel*/
	if ((pstrHostIFconnectAttr->u8channel >= 1) && (pstrHostIFconnectAttr->u8channel <= 14)) {
		*(pu8CurrByte++) = pstrHostIFconnectAttr->u8channel;
	} else {
		PRINT_ER("Channel out of range\n");
		*(pu8CurrByte++) = 0xFF;
	}
	/* Cap Info*/
	*(pu8CurrByte++)  = (ptstrJoinBssParam->cap_info) & 0xFF;
	*(pu8CurrByte++)  = ((ptstrJoinBssParam->cap_info) >> 8) & 0xFF;
	PRINT_D(HOSTINF_DBG, "* Cap Info %0x*\n", (*(pu8CurrByte - 2) | ((*(pu8CurrByte - 1)) << 8)));

	/* sa*/
	if (pstrHostIFconnectAttr->pu8bssid != NULL)
		memcpy(pu8CurrByte, pstrHostIFconnectAttr->pu8bssid, 6);
	pu8CurrByte += 6;

	/* bssid*/
	if (pstrHostIFconnectAttr->pu8bssid != NULL)
		memcpy(pu8CurrByte, pstrHostIFconnectAttr->pu8bssid, 6);
	pu8CurrByte += 6;

	/* Beacon Period*/
	*(pu8CurrByte++)  = (ptstrJoinBssParam->beacon_period) & 0xFF;
	*(pu8CurrByte++)  = ((ptstrJoinBssParam->beacon_period) >> 8) & 0xFF;
	PRINT_D(HOSTINF_DBG, "* Beacon Period %d*\n", (*(pu8CurrByte - 2) | ((*(pu8CurrByte - 1)) << 8)));
	/* DTIM Period*/
	*(pu8CurrByte++)  =  ptstrJoinBssParam->dtim_period;
	PRINT_D(HOSTINF_DBG, "* DTIM Period %d*\n", (*(pu8CurrByte - 1)));
	/* Supported rates*/
	memcpy(pu8CurrByte, ptstrJoinBssParam->supp_rates, MAX_RATES_SUPPORTED + 1);
	pu8CurrByte += (MAX_RATES_SUPPORTED + 1);

	/* wmm cap*/
	*(pu8CurrByte++)  =  ptstrJoinBssParam->wmm_cap;
	PRINT_D(HOSTINF_DBG, "* wmm cap%d*\n", (*(pu8CurrByte - 1)));
	/* uapsd cap*/
	*(pu8CurrByte++)  = ptstrJoinBssParam->uapsd_cap;

	/* ht cap*/
	*(pu8CurrByte++)  = ptstrJoinBssParam->ht_capable;
	/* copy this information to the user request */
	pstrWFIDrv->strWILC_UsrConnReq.IsHTCapable = ptstrJoinBssParam->ht_capable;

	/* rsn found*/
	*(pu8CurrByte++)  =  ptstrJoinBssParam->rsn_found;
	PRINT_D(HOSTINF_DBG, "* rsn found %d*\n", *(pu8CurrByte - 1));
	/* rsn group policy*/
	*(pu8CurrByte++)  =  ptstrJoinBssParam->rsn_grp_policy;
	PRINT_D(HOSTINF_DBG, "* rsn group policy %0x*\n", (*(pu8CurrByte - 1)));
	/* mode_802_11i*/
	*(pu8CurrByte++) =  ptstrJoinBssParam->mode_802_11i;
	PRINT_D(HOSTINF_DBG, "* mode_802_11i %d*\n", (*(pu8CurrByte - 1)));
	/* rsn pcip policy*/
	memcpy(pu8CurrByte, ptstrJoinBssParam->rsn_pcip_policy, sizeof(ptstrJoinBssParam->rsn_pcip_policy));
	pu8CurrByte += sizeof(ptstrJoinBssParam->rsn_pcip_policy);

	/* rsn auth policy*/
	memcpy(pu8CurrByte, ptstrJoinBssParam->rsn_auth_policy, sizeof(ptstrJoinBssParam->rsn_auth_policy));
	pu8CurrByte += sizeof(ptstrJoinBssParam->rsn_auth_policy);

	/* rsn auth policy*/
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

	/* keep the buffer at the start of the allocated pointer to use it with the free*/
	pu8CurrByte = strWIDList[u32WidsCount].ps8WidVal;
	u32WidsCount++;

	/* A temporary workaround to avoid handling the misleading MAC_DISCONNECTED raised from the
	 *   firmware at chip reset when processing the WIDs of the Connect Request.
	 *   (This workaround should be removed in the future when the Chip reset of the Connect WIDs is disabled) */
	/* ////////////////////// */
	gu32WidConnRstHack = 0;
	/* ////////////////////// */

	if (memcmp("DIRECT-", pstrHostIFconnectAttr->pu8ssid, 7)) {
		memcpy(gu8FlushedJoinReq, pu8CurrByte, gu32FlushedJoinReqSize);
		gu8FlushedJoinReqDrvHandler = pstrWFIDrv;
	}

	PRINT_D(GENERIC_DBG, "send HOST_IF_WAITING_CONN_RESP\n");

	if (pstrHostIFconnectAttr->pu8bssid != NULL) {
		memcpy(u8ConnectedSSID, pstrHostIFconnectAttr->pu8bssid, ETH_ALEN);

		PRINT_D(GENERIC_DBG, "save Bssid = %pM\n", pstrHostIFconnectAttr->pu8bssid);
		PRINT_D(GENERIC_DBG, "save bssid = %pM\n", u8ConnectedSSID);
	}

	s32Error = send_config_pkt(SET_CFG, strWIDList, u32WidsCount, false,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error) {
		PRINT_ER("failed to send config packet\n");
		s32Error = -EFAULT;
		goto ERRORHANDLER;
	} else {
		PRINT_D(GENERIC_DBG, "set HOST_IF_WAITING_CONN_RESP\n");
		pstrWFIDrv->enuHostIFstate = HOST_IF_WAITING_CONN_RESP;
	}

ERRORHANDLER:
	if (s32Error) {
		tstrConnectInfo strConnectInfo;

		del_timer(&pstrWFIDrv->hConnectTimer);

		PRINT_D(HOSTINF_DBG, "could not start connecting to the required network\n");

		memset(&strConnectInfo, 0, sizeof(tstrConnectInfo));

		if (pstrHostIFconnectAttr->pfConnectResult != NULL) {
			if (pstrHostIFconnectAttr->pu8bssid != NULL)
				memcpy(strConnectInfo.au8bssid, pstrHostIFconnectAttr->pu8bssid, 6);

			if (pstrHostIFconnectAttr->pu8IEs != NULL) {
				strConnectInfo.ReqIEsLen = pstrHostIFconnectAttr->IEsLen;
				strConnectInfo.pu8ReqIEs = kmalloc(pstrHostIFconnectAttr->IEsLen, GFP_KERNEL);
				memcpy(strConnectInfo.pu8ReqIEs,
					    pstrHostIFconnectAttr->pu8IEs,
					    pstrHostIFconnectAttr->IEsLen);
			}

			pstrHostIFconnectAttr->pfConnectResult(CONN_DISCONN_EVENT_CONN_RESP,
							       &strConnectInfo,
							       MAC_DISCONNECTED,
							       NULL,
							       pstrHostIFconnectAttr->pvUserArg);
			/*Change state to idle*/
			pstrWFIDrv->enuHostIFstate = HOST_IF_IDLE;
			/* Deallocation */
			if (strConnectInfo.pu8ReqIEs != NULL) {
				kfree(strConnectInfo.pu8ReqIEs);
				strConnectInfo.pu8ReqIEs = NULL;
			}

		} else {
			PRINT_ER("Connect callback function pointer is NULL\n");
		}
	}

	PRINT_D(HOSTINF_DBG, "Deallocating connection parameters\n");
	/* Deallocate pstrHostIFconnectAttr->pu8bssid which was prevoisuly allocated by the sending thread */
	if (pstrHostIFconnectAttr->pu8bssid != NULL) {
		kfree(pstrHostIFconnectAttr->pu8bssid);
		pstrHostIFconnectAttr->pu8bssid = NULL;
	}

	/* Deallocate pstrHostIFconnectAttr->pu8ssid which was prevoisuly allocated by the sending thread */
	if (pstrHostIFconnectAttr->pu8ssid != NULL) {
		kfree(pstrHostIFconnectAttr->pu8ssid);
		pstrHostIFconnectAttr->pu8ssid = NULL;
	}

	/* Deallocate pstrHostIFconnectAttr->pu8IEs which was prevoisuly allocated by the sending thread */
	if (pstrHostIFconnectAttr->pu8IEs != NULL) {
		kfree(pstrHostIFconnectAttr->pu8IEs);
		pstrHostIFconnectAttr->pu8IEs = NULL;
	}

	if (pu8CurrByte != NULL)
		kfree(pu8CurrByte);
	return s32Error;
}

/**
 *  @brief                      Handle_FlushConnect
 *  @details            Sending config packet to firmware to flush an old connection
 *                              after switching FW from station one to hybrid one
 *  @param[in]          void * drvHandler
 *  @return             Error code.
 *  @author		Amr Abdel-Moghny
 *  @date			19 DEC 2013
 *  @version		8.0
 */

static s32 Handle_FlushConnect(tstrWILC_WFIDrv *drvHandler)
{
	s32 s32Error = 0;
	tstrWID strWIDList[5];
	u32 u32WidsCount = 0;
	u8 *pu8CurrByte = NULL;


	/* IEs to be inserted in Association Request */
	strWIDList[u32WidsCount].u16WIDid = WID_INFO_ELEMENT_ASSOCIATE;
	strWIDList[u32WidsCount].enuWIDtype = WID_BIN_DATA;
	strWIDList[u32WidsCount].ps8WidVal = gu8FlushedInfoElemAsoc;
	strWIDList[u32WidsCount].s32ValueSize = gu32FlushedInfoElemAsocSize;
	u32WidsCount++;

	strWIDList[u32WidsCount].u16WIDid = (u16)WID_11I_MODE;
	strWIDList[u32WidsCount].enuWIDtype = WID_CHAR;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(char);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&(gu8Flushed11iMode));
	u32WidsCount++;



	strWIDList[u32WidsCount].u16WIDid = (u16)WID_AUTH_TYPE;
	strWIDList[u32WidsCount].enuWIDtype = WID_CHAR;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(char);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&gu8FlushedAuthType);
	u32WidsCount++;

	strWIDList[u32WidsCount].u16WIDid = (u16)WID_JOIN_REQ_EXTENDED;
	strWIDList[u32WidsCount].enuWIDtype = WID_STR;
	strWIDList[u32WidsCount].s32ValueSize = gu32FlushedJoinReqSize;
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)gu8FlushedJoinReq;
	pu8CurrByte = strWIDList[u32WidsCount].ps8WidVal;

	pu8CurrByte += FLUSHED_BYTE_POS;
	*(pu8CurrByte) = FLUSHED_JOIN_REQ;

	u32WidsCount++;

	s32Error = send_config_pkt(SET_CFG, strWIDList, u32WidsCount, false,
				   get_id_from_handler(gu8FlushedJoinReqDrvHandler));
	if (s32Error) {
		PRINT_ER("failed to send config packet\n");
		s32Error = -EINVAL;
	}

	return s32Error;
}

/**
 *  @brief                 Handle_ConnectTimeout
 *  @details       Call connect notification callback function indicating connection failure
 *  @param[in]    NONE
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_ConnectTimeout(tstrWILC_WFIDrv *drvHandler)
{
	s32 s32Error = 0;
	tstrConnectInfo strConnectInfo;
	tstrWID strWID;
	u16 u16DummyReasonCode = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *) drvHandler;

	if (pstrWFIDrv == NULL)	{
		PRINT_ER("Driver handler is NULL\n");
		return s32Error;
	}

	pstrWFIDrv->enuHostIFstate = HOST_IF_IDLE;

	gbScanWhileConnected = false;


	memset(&strConnectInfo, 0, sizeof(tstrConnectInfo));


	/* First, we will notify the upper layer with the Connection failure {through the Connect Callback function},
	 *   then we will notify our firmware also with the Connection failure {through sending to it Cfg packet carrying
	 *   WID_DISCONNECT} */
	if (pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult != NULL)	{
		if (pstrWFIDrv->strWILC_UsrConnReq.pu8bssid != NULL) {
			memcpy(strConnectInfo.au8bssid,
				    pstrWFIDrv->strWILC_UsrConnReq.pu8bssid, 6);
		}

		if (pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) {
			strConnectInfo.ReqIEsLen = pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen;
			strConnectInfo.pu8ReqIEs = kmalloc(pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen, GFP_KERNEL);
			memcpy(strConnectInfo.pu8ReqIEs,
				    pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs,
				    pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen);
		}

		pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult(CONN_DISCONN_EVENT_CONN_RESP,
								   &strConnectInfo,
								   MAC_DISCONNECTED,
								   NULL,
								   pstrWFIDrv->strWILC_UsrConnReq.u32UserConnectPvoid);

		/* Deallocation of strConnectInfo.pu8ReqIEs */
		if (strConnectInfo.pu8ReqIEs != NULL) {
			kfree(strConnectInfo.pu8ReqIEs);
			strConnectInfo.pu8ReqIEs = NULL;
		}
	} else {
		PRINT_ER("Connect callback function pointer is NULL\n");
	}

	/* Here we will notify our firmware also with the Connection failure {through sending to it Cfg packet carrying
	 *   WID_DISCONNECT} */
	strWID.u16WIDid = (u16)WID_DISCONNECT;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = (s8 *)&u16DummyReasonCode;
	strWID.s32ValueSize = sizeof(char);

	PRINT_D(HOSTINF_DBG, "Sending disconnect request\n");

	s32Error = send_config_pkt(SET_CFG, &strWID, 1, false,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error)
		PRINT_ER("Failed to send dissconect config packet\n");

	/* Deallocation of the Saved Connect Request in the global Handle */
	pstrWFIDrv->strWILC_UsrConnReq.ssidLen = 0;
	if (pstrWFIDrv->strWILC_UsrConnReq.pu8ssid != NULL) {
		kfree(pstrWFIDrv->strWILC_UsrConnReq.pu8ssid);
		pstrWFIDrv->strWILC_UsrConnReq.pu8ssid = NULL;
	}

	if (pstrWFIDrv->strWILC_UsrConnReq.pu8bssid != NULL) {
		kfree(pstrWFIDrv->strWILC_UsrConnReq.pu8bssid);
		pstrWFIDrv->strWILC_UsrConnReq.pu8bssid = NULL;
	}

	pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen = 0;
	if (pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) {
		kfree(pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs);
		pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs = NULL;
	}

	memset(u8ConnectedSSID, 0, ETH_ALEN);
	/*Freeing flushed join request params on connect timeout*/
	if (gu8FlushedJoinReq != NULL && gu8FlushedJoinReqDrvHandler == drvHandler) {
		kfree(gu8FlushedJoinReq);
		gu8FlushedJoinReq = NULL;
	}
	if (gu8FlushedInfoElemAsoc != NULL && gu8FlushedJoinReqDrvHandler == drvHandler) {
		kfree(gu8FlushedInfoElemAsoc);
		gu8FlushedInfoElemAsoc = NULL;
	}

	return s32Error;
}

/**
 *  @brief Handle_RcvdNtwrkInfo
 *  @details       Handling received network information
 *  @param[in]    struct rcvd_net_info *pstrRcvdNetworkInfo
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_RcvdNtwrkInfo(tstrWILC_WFIDrv *drvHandler,
				struct rcvd_net_info *pstrRcvdNetworkInfo)
{
	u32 i;
	bool bNewNtwrkFound;



	s32 s32Error = 0;
	tstrNetworkInfo *pstrNetworkInfo = NULL;
	void *pJoinParams = NULL;

	tstrWILC_WFIDrv *pstrWFIDrv  = (tstrWILC_WFIDrv *)drvHandler;



	bNewNtwrkFound = true;
	PRINT_INFO(HOSTINF_DBG, "Handling received network info\n");

	/*if there is a an ongoing scan request*/
	if (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult) {
		PRINT_D(HOSTINF_DBG, "State: Scanning, parsing network information received\n");
		parse_network_info(pstrRcvdNetworkInfo->pu8Buffer, &pstrNetworkInfo);
		if ((pstrNetworkInfo == NULL)
		    || (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult == NULL)) {
			PRINT_ER("driver is null\n");
			s32Error = -EINVAL;
			goto done;
		}

		/* check whether this network is discovered before */
		for (i = 0; i < pstrWFIDrv->strWILC_UsrScanReq.u32RcvdChCount; i++) {

			if ((pstrWFIDrv->strWILC_UsrScanReq.astrFoundNetworkInfo[i].au8bssid != NULL) &&
			    (pstrNetworkInfo->au8bssid != NULL)) {
				if (memcmp(pstrWFIDrv->strWILC_UsrScanReq.astrFoundNetworkInfo[i].au8bssid,
						pstrNetworkInfo->au8bssid, 6) == 0) {
					if (pstrNetworkInfo->s8rssi <= pstrWFIDrv->strWILC_UsrScanReq.astrFoundNetworkInfo[i].s8rssi) {
						/*we have already found this network with better rssi, so keep the old cached one and don't
						 *  send anything to the upper layer */
						PRINT_D(HOSTINF_DBG, "Network previously discovered\n");
						goto done;
					} else {
						/* here the same already found network is found again but with a better rssi, so just update
						 *   the rssi for this cached network and send this updated network to the upper layer but
						 *   don't add a new record for it */
						pstrWFIDrv->strWILC_UsrScanReq.astrFoundNetworkInfo[i].s8rssi = pstrNetworkInfo->s8rssi;
						bNewNtwrkFound = false;
						break;
					}
				}
			}
		}

		if (bNewNtwrkFound == true) {
			/* here it is confirmed that it is a new discovered network,
			 * so add its record then call the User CallBack function */

			PRINT_D(HOSTINF_DBG, "New network found\n");

			if (pstrWFIDrv->strWILC_UsrScanReq.u32RcvdChCount < MAX_NUM_SCANNED_NETWORKS) {
				pstrWFIDrv->strWILC_UsrScanReq.astrFoundNetworkInfo[pstrWFIDrv->strWILC_UsrScanReq.u32RcvdChCount].s8rssi = pstrNetworkInfo->s8rssi;

				if ((pstrWFIDrv->strWILC_UsrScanReq.astrFoundNetworkInfo[pstrWFIDrv->strWILC_UsrScanReq.u32RcvdChCount].au8bssid != NULL)
				    && (pstrNetworkInfo->au8bssid != NULL)) {
					memcpy(pstrWFIDrv->strWILC_UsrScanReq.astrFoundNetworkInfo[pstrWFIDrv->strWILC_UsrScanReq.u32RcvdChCount].au8bssid,
						    pstrNetworkInfo->au8bssid, 6);

					pstrWFIDrv->strWILC_UsrScanReq.u32RcvdChCount++;

					pstrNetworkInfo->bNewNetwork = true;
					/* add new BSS to JoinBssTable */
					pJoinParams = host_int_ParseJoinBssParam(pstrNetworkInfo);

					pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult(SCAN_EVENT_NETWORK_FOUND, pstrNetworkInfo,
											pstrWFIDrv->strWILC_UsrScanReq.u32UserScanPvoid,
											pJoinParams);


				}
			} else {
				PRINT_WRN(HOSTINF_DBG, "Discovered networks exceeded max. limit\n");
			}
		} else {
			pstrNetworkInfo->bNewNetwork = false;
			/* just call the User CallBack function to send the same discovered network with its updated RSSI */
			pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult(SCAN_EVENT_NETWORK_FOUND, pstrNetworkInfo,
									pstrWFIDrv->strWILC_UsrScanReq.u32UserScanPvoid, NULL);
		}
	}

done:
	/* Deallocate pstrRcvdNetworkInfo->pu8Buffer which was prevoisuly allocated by the sending thread */
	if (pstrRcvdNetworkInfo->pu8Buffer != NULL) {
		kfree(pstrRcvdNetworkInfo->pu8Buffer);
		pstrRcvdNetworkInfo->pu8Buffer = NULL;
	}

	/*free structure allocated*/
	if (pstrNetworkInfo != NULL) {
		DeallocateNetworkInfo(pstrNetworkInfo);
		pstrNetworkInfo = NULL;
	}

	return s32Error;
}

/**
 *  @brief Handle_RcvdGnrlAsyncInfo
 *  @details       Handling received asynchrous general network information
 *  @param[in]     struct rcvd_async_info *pstrRcvdGnrlAsyncInfo
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_RcvdGnrlAsyncInfo(tstrWILC_WFIDrv *drvHandler,
				    struct rcvd_async_info *pstrRcvdGnrlAsyncInfo)
{
	/* TODO: mostafa: till now, this function just handles only the received mac status msg, */
	/*				 which carries only 1 WID which have WID ID = WID_STATUS */
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
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *) drvHandler;

	if (!pstrWFIDrv) {
		PRINT_ER("Driver handler is NULL\n");
		return -ENODEV;
	}
	PRINT_D(GENERIC_DBG, "Current State = %d,Received state = %d\n", pstrWFIDrv->enuHostIFstate,
		pstrRcvdGnrlAsyncInfo->pu8Buffer[7]);

	if ((pstrWFIDrv->enuHostIFstate == HOST_IF_WAITING_CONN_RESP) ||
	    (pstrWFIDrv->enuHostIFstate == HOST_IF_CONNECTED) ||
	    pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult) {
		if ((pstrRcvdGnrlAsyncInfo->pu8Buffer == NULL) ||
		    (pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult == NULL)) {
			PRINT_ER("driver is null\n");
			return -EINVAL;
		}

		u8MsgType = pstrRcvdGnrlAsyncInfo->pu8Buffer[0];

		/* Check whether the received message type is 'I' */
		if ('I' != u8MsgType) {
			PRINT_ER("Received Message format incorrect.\n");
			return -EFAULT;
		}

		/* Extract message ID */
		u8MsgID = pstrRcvdGnrlAsyncInfo->pu8Buffer[1];

		/* Extract message Length */
		u16MsgLen = MAKE_WORD16(pstrRcvdGnrlAsyncInfo->pu8Buffer[2], pstrRcvdGnrlAsyncInfo->pu8Buffer[3]);

		/* Extract WID ID [expected to be = WID_STATUS] */
		u16WidID = MAKE_WORD16(pstrRcvdGnrlAsyncInfo->pu8Buffer[4], pstrRcvdGnrlAsyncInfo->pu8Buffer[5]);

		/* Extract WID Length [expected to be = 1] */
		u8WidLen = pstrRcvdGnrlAsyncInfo->pu8Buffer[6];

		/* get the WID value [expected to be one of two values: either MAC_CONNECTED = (1) or MAC_DISCONNECTED = (0)] */
		u8MacStatus  = pstrRcvdGnrlAsyncInfo->pu8Buffer[7];
		u8MacStatusReasonCode = pstrRcvdGnrlAsyncInfo->pu8Buffer[8];
		u8MacStatusAdditionalInfo = pstrRcvdGnrlAsyncInfo->pu8Buffer[9];
		PRINT_INFO(HOSTINF_DBG, "Recieved MAC status = %d with Reason = %d , Info = %d\n", u8MacStatus, u8MacStatusReasonCode, u8MacStatusAdditionalInfo);
		if (pstrWFIDrv->enuHostIFstate == HOST_IF_WAITING_CONN_RESP) {
			/* our station had sent Association Request frame, so here it will get the Association Response frame then parse it */
			u32 u32RcvdAssocRespInfoLen;
			tstrConnectRespInfo *pstrConnectRespInfo = NULL;

			PRINT_D(HOSTINF_DBG, "Recieved MAC status = %d with Reason = %d , Code = %d\n", u8MacStatus, u8MacStatusReasonCode, u8MacStatusAdditionalInfo);

			memset(&strConnectInfo, 0, sizeof(tstrConnectInfo));

			if (u8MacStatus == MAC_CONNECTED) {
				memset(gapu8RcvdAssocResp, 0, MAX_ASSOC_RESP_FRAME_SIZE);

				host_int_get_assoc_res_info(pstrWFIDrv,
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
						/* use the necessary parsed Info from the Received Association Response */
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

						/* deallocate the Assoc. Resp. parsed structure as it is not needed anymore */
						if (pstrConnectRespInfo != NULL) {
							DeallocateAssocRespInfo(pstrConnectRespInfo);
							pstrConnectRespInfo = NULL;
						}
					}
				}
			}

			/* The station has just received mac status and it also received assoc. response which
			 *   it was waiting for.
			 *   So check first the matching between the received mac status and the received status code in Asoc Resp */
			if ((u8MacStatus == MAC_CONNECTED) &&
			    (strConnectInfo.u16ConnectStatus != SUCCESSFUL_STATUSCODE))	{
				PRINT_ER("Received MAC status is MAC_CONNECTED while the received status code in Asoc Resp is not SUCCESSFUL_STATUSCODE\n");
				memset(u8ConnectedSSID, 0, ETH_ALEN);

			} else if (u8MacStatus == MAC_DISCONNECTED)    {
				PRINT_ER("Received MAC status is MAC_DISCONNECTED\n");
				memset(u8ConnectedSSID, 0, ETH_ALEN);
			}

			/* TODO: mostafa: correct BSSID should be retrieved from actual BSSID received from AP */
			/*               through a structure of type tstrConnectRespInfo */
			if (pstrWFIDrv->strWILC_UsrConnReq.pu8bssid != NULL) {
				PRINT_D(HOSTINF_DBG, "Retrieving actual BSSID from AP\n");
				memcpy(strConnectInfo.au8bssid, pstrWFIDrv->strWILC_UsrConnReq.pu8bssid, 6);

				if ((u8MacStatus == MAC_CONNECTED) &&
				    (strConnectInfo.u16ConnectStatus == SUCCESSFUL_STATUSCODE))	{
					memcpy(pstrWFIDrv->au8AssociatedBSSID,
						    pstrWFIDrv->strWILC_UsrConnReq.pu8bssid, ETH_ALEN);
				}
			}


			if (pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) {
				strConnectInfo.ReqIEsLen = pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen;
				strConnectInfo.pu8ReqIEs = kmalloc(pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen, GFP_KERNEL);
				memcpy(strConnectInfo.pu8ReqIEs,
					    pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs,
					    pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen);
			}


			del_timer(&pstrWFIDrv->hConnectTimer);
			pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult(CONN_DISCONN_EVENT_CONN_RESP,
									   &strConnectInfo,
									   u8MacStatus,
									   NULL,
									   pstrWFIDrv->strWILC_UsrConnReq.u32UserConnectPvoid);


			/* if received mac status is MAC_CONNECTED and
			 *  received status code in Asoc Resp is SUCCESSFUL_STATUSCODE, change state to CONNECTED
			 *  else change state to IDLE */
			if ((u8MacStatus == MAC_CONNECTED) &&
			    (strConnectInfo.u16ConnectStatus == SUCCESSFUL_STATUSCODE))	{
				host_int_set_power_mgmt(pstrWFIDrv, 0, 0);

				PRINT_D(HOSTINF_DBG, "MAC status : CONNECTED and Connect Status : Successful\n");
				pstrWFIDrv->enuHostIFstate = HOST_IF_CONNECTED;

				PRINT_D(GENERIC_DBG, "Obtaining an IP, Disable Scan\n");
				g_obtainingIP = true;
				mod_timer(&hDuringIpTimer,
					  jiffies + msecs_to_jiffies(10000));

				/* open a BA session if possible */
				/* if(pstrWFIDrv->strWILC_UsrConnReq.IsHTCapable) */
				/* host_int_addBASession(pstrWFIDrv->strWILC_UsrConnReq.pu8bssid,0, */
				/* BA_SESSION_DEFAULT_BUFFER_SIZE,BA_SESSION_DEFAULT_TIMEOUT); */
			} else {
				PRINT_D(HOSTINF_DBG, "MAC status : %d and Connect Status : %d\n", u8MacStatus, strConnectInfo.u16ConnectStatus);
				pstrWFIDrv->enuHostIFstate = HOST_IF_IDLE;
				gbScanWhileConnected = false;
			}

			/* Deallocation */
			if (strConnectInfo.pu8RespIEs != NULL) {
				kfree(strConnectInfo.pu8RespIEs);
				strConnectInfo.pu8RespIEs = NULL;
			}

			if (strConnectInfo.pu8ReqIEs != NULL) {
				kfree(strConnectInfo.pu8ReqIEs);
				strConnectInfo.pu8ReqIEs = NULL;
			}


			pstrWFIDrv->strWILC_UsrConnReq.ssidLen = 0;
			if (pstrWFIDrv->strWILC_UsrConnReq.pu8ssid != NULL) {
				kfree(pstrWFIDrv->strWILC_UsrConnReq.pu8ssid);
				pstrWFIDrv->strWILC_UsrConnReq.pu8ssid = NULL;
			}

			if (pstrWFIDrv->strWILC_UsrConnReq.pu8bssid != NULL) {
				kfree(pstrWFIDrv->strWILC_UsrConnReq.pu8bssid);
				pstrWFIDrv->strWILC_UsrConnReq.pu8bssid = NULL;
			}

			pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen = 0;
			if (pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) {
				kfree(pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs);
				pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs = NULL;
			}

		} else if ((u8MacStatus == MAC_DISCONNECTED) &&
			   (pstrWFIDrv->enuHostIFstate == HOST_IF_CONNECTED)) {
			/* Disassociation or Deauthentication frame has been received */
			PRINT_D(HOSTINF_DBG, "Received MAC_DISCONNECTED from the FW\n");

			memset(&strDisconnectNotifInfo, 0, sizeof(tstrDisconnectNotifInfo));

			if (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult) {
				PRINT_D(HOSTINF_DBG, "\n\n<< Abort the running OBSS Scan >>\n\n");
				del_timer(&pstrWFIDrv->hScanTimer);
				Handle_ScanDone((void *)pstrWFIDrv, SCAN_EVENT_ABORTED);
			}

			strDisconnectNotifInfo.u16reason = 0;
			strDisconnectNotifInfo.ie = NULL;
			strDisconnectNotifInfo.ie_len = 0;

			if (pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult != NULL)	{
				g_obtainingIP = false;
				host_int_set_power_mgmt(pstrWFIDrv, 0, 0);

				pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult(CONN_DISCONN_EVENT_DISCONN_NOTIF,
										   NULL,
										   0,
										   &strDisconnectNotifInfo,
										   pstrWFIDrv->strWILC_UsrConnReq.u32UserConnectPvoid);

			} else {
				PRINT_ER("Connect result callback function is NULL\n");
			}

			memset(pstrWFIDrv->au8AssociatedBSSID, 0, ETH_ALEN);


			/* Deallocation */

			/* if Information Elements were retrieved from the Received deauth/disassoc frame, then they
			 *  should be deallocated here */
			/*
			 * if(strDisconnectNotifInfo.ie != NULL)
			 * {
			 *      kfree(strDisconnectNotifInfo.ie);
			 *      strDisconnectNotifInfo.ie = NULL;
			 * }
			 */

			pstrWFIDrv->strWILC_UsrConnReq.ssidLen = 0;
			if (pstrWFIDrv->strWILC_UsrConnReq.pu8ssid != NULL) {
				kfree(pstrWFIDrv->strWILC_UsrConnReq.pu8ssid);
				pstrWFIDrv->strWILC_UsrConnReq.pu8ssid = NULL;
			}

			if (pstrWFIDrv->strWILC_UsrConnReq.pu8bssid != NULL) {
				kfree(pstrWFIDrv->strWILC_UsrConnReq.pu8bssid);
				pstrWFIDrv->strWILC_UsrConnReq.pu8bssid = NULL;
			}

			pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen = 0;
			if (pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) {
				kfree(pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs);
				pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs = NULL;
			}

			/*Freeing flushed join request params on receiving*/
			/*MAC_DISCONNECTED while connected*/
			if (gu8FlushedJoinReq != NULL && gu8FlushedJoinReqDrvHandler == drvHandler) {
				kfree(gu8FlushedJoinReq);
				gu8FlushedJoinReq = NULL;
			}
			if (gu8FlushedInfoElemAsoc != NULL && gu8FlushedJoinReqDrvHandler == drvHandler) {
				kfree(gu8FlushedInfoElemAsoc);
				gu8FlushedInfoElemAsoc = NULL;
			}

			pstrWFIDrv->enuHostIFstate = HOST_IF_IDLE;
			gbScanWhileConnected = false;

		} else if ((u8MacStatus == MAC_DISCONNECTED) &&
			   (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult != NULL)) {
			PRINT_D(HOSTINF_DBG, "Received MAC_DISCONNECTED from the FW while scanning\n");
			PRINT_D(HOSTINF_DBG, "\n\n<< Abort the running Scan >>\n\n");
			/*Abort the running scan*/
			del_timer(&pstrWFIDrv->hScanTimer);
			if (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult)
				Handle_ScanDone(pstrWFIDrv, SCAN_EVENT_ABORTED);

		}

	}

	/* Deallocate pstrRcvdGnrlAsyncInfo->pu8Buffer which was prevoisuly allocated by the sending thread */
	if (pstrRcvdGnrlAsyncInfo->pu8Buffer != NULL) {
		kfree(pstrRcvdGnrlAsyncInfo->pu8Buffer);
		pstrRcvdGnrlAsyncInfo->pu8Buffer = NULL;
	}

	return s32Error;
}

/**
 *  @brief Handle_Key
 *  @details       Sending config packet to firmware to set key
 *  @param[in]    struct key_attr *pstrHostIFkeyAttr
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static int Handle_Key(tstrWILC_WFIDrv *drvHandler,
		      struct key_attr *pstrHostIFkeyAttr)
{
	s32 s32Error = 0;
	tstrWID strWID;
	tstrWID strWIDList[5];
	u8 i;
	u8 *pu8keybuf;
	s8 s8idxarray[1];
	s8 ret = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;


	switch (pstrHostIFkeyAttr->enuKeyType) {


	case WEP:

		if (pstrHostIFkeyAttr->u8KeyAction & ADDKEY_AP)	{

			PRINT_D(HOSTINF_DBG, "Handling WEP key\n");
			PRINT_D(GENERIC_DBG, "ID Hostint is %d\n", (pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx));
			strWIDList[0].u16WIDid = (u16)WID_11I_MODE;
			strWIDList[0].enuWIDtype = WID_CHAR;
			strWIDList[0].s32ValueSize = sizeof(char);
			strWIDList[0].ps8WidVal = (s8 *)(&(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8mode));

			strWIDList[1].u16WIDid     = WID_AUTH_TYPE;
			strWIDList[1].enuWIDtype  = WID_CHAR;
			strWIDList[1].s32ValueSize = sizeof(char);
			strWIDList[1].ps8WidVal = (s8 *)(&(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.tenuAuth_type));

			strWIDList[2].u16WIDid	= (u16)WID_KEY_ID;
			strWIDList[2].enuWIDtype	= WID_CHAR;

			strWIDList[2].ps8WidVal	= (s8 *)(&(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx));
			strWIDList[2].s32ValueSize = sizeof(char);


			pu8keybuf = kmalloc(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen, GFP_KERNEL);


			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send Key\n");
				return -1;
			}

			memcpy(pu8keybuf, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey,
				    pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen);


			kfree(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey);

			strWIDList[3].u16WIDid = (u16)WID_WEP_KEY_VALUE;
			strWIDList[3].enuWIDtype = WID_STR;
			strWIDList[3].s32ValueSize = pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen;
			strWIDList[3].ps8WidVal = (s8 *)pu8keybuf;


			s32Error = send_config_pkt(SET_CFG, strWIDList, 4, true,
						   get_id_from_handler(pstrWFIDrv));
			kfree(pu8keybuf);


		}

		if (pstrHostIFkeyAttr->u8KeyAction & ADDKEY) {
			PRINT_D(HOSTINF_DBG, "Handling WEP key\n");
			pu8keybuf = kmalloc(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen + 2, GFP_KERNEL);
			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send Key\n");
				return -1;
			}
			pu8keybuf[0] = pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx;

			memcpy(pu8keybuf + 1, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen, 1);

			memcpy(pu8keybuf + 2, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey,
				    pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen);

			kfree(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey);

			strWID.u16WIDid	= (u16)WID_ADD_WEP_KEY;
			strWID.enuWIDtype	= WID_STR;
			strWID.ps8WidVal	= (s8 *)pu8keybuf;
			strWID.s32ValueSize = pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen + 2;

			s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
						   get_id_from_handler(pstrWFIDrv));
			kfree(pu8keybuf);
		} else if (pstrHostIFkeyAttr->u8KeyAction & REMOVEKEY)	  {

			PRINT_D(HOSTINF_DBG, "Removing key\n");
			strWID.u16WIDid	= (u16)WID_REMOVE_WEP_KEY;
			strWID.enuWIDtype	= WID_STR;

			s8idxarray[0] = (s8)pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx;
			strWID.ps8WidVal = s8idxarray;
			strWID.s32ValueSize = 1;

			s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
						   get_id_from_handler(pstrWFIDrv));
		} else {
			strWID.u16WIDid	= (u16)WID_KEY_ID;
			strWID.enuWIDtype	= WID_CHAR;
			strWID.ps8WidVal	= (s8 *)(&(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx));
			strWID.s32ValueSize = sizeof(char);

			PRINT_D(HOSTINF_DBG, "Setting default key index\n");

			s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
						   get_id_from_handler(pstrWFIDrv));
		}
		up(&(pstrWFIDrv->hSemTestKeyBlock));
		break;

	case WPARxGtk:
		if (pstrHostIFkeyAttr->u8KeyAction & ADDKEY_AP)	{
			pu8keybuf = kmalloc(RX_MIC_KEY_MSG_LEN, GFP_KERNEL);
			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send RxGTK Key\n");
				ret = -1;
				goto _WPARxGtk_end_case_;
			}

			memset(pu8keybuf, 0, RX_MIC_KEY_MSG_LEN);


			/*|----------------------------------------------------------------------------|
			 * |Sta Address | Key RSC | KeyID | Key Length | Temporal Key	| Rx Michael Key |
			 * |------------|---------|-------|------------|---------------|----------------|
			 |	6 bytes	 | 8 byte  |1 byte |  1 byte	|   16 bytes	|	  8 bytes	 |*/



			if (pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8seq != NULL)
				memcpy(pu8keybuf + 6, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8seq, 8);


			memcpy(pu8keybuf + 14, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8keyidx, 1);

			memcpy(pu8keybuf + 15, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen, 1);

			memcpy(pu8keybuf + 16, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8key,
				    pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen);
			/* pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Ciphermode =  0X51; */
			strWIDList[0].u16WIDid = (u16)WID_11I_MODE;
			strWIDList[0].enuWIDtype = WID_CHAR;
			strWIDList[0].s32ValueSize = sizeof(char);
			strWIDList[0].ps8WidVal = (s8 *)(&(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Ciphermode));

			strWIDList[1].u16WIDid	= (u16)WID_ADD_RX_GTK;
			strWIDList[1].enuWIDtype	= WID_STR;
			strWIDList[1].ps8WidVal	= (s8 *)pu8keybuf;
			strWIDList[1].s32ValueSize = RX_MIC_KEY_MSG_LEN;

			s32Error = send_config_pkt(SET_CFG, strWIDList, 2, true,
						   get_id_from_handler(pstrWFIDrv));

			kfree(pu8keybuf);

			/* ////////////////////////// */
			up(&(pstrWFIDrv->hSemTestKeyBlock));
			/* ///////////////////////// */
		}

		if (pstrHostIFkeyAttr->u8KeyAction & ADDKEY) {
			PRINT_D(HOSTINF_DBG, "Handling group key(Rx) function\n");

			pu8keybuf = kmalloc(RX_MIC_KEY_MSG_LEN, GFP_KERNEL);
			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send RxGTK Key\n");
				ret = -1;
				goto _WPARxGtk_end_case_;
			}

			memset(pu8keybuf, 0, RX_MIC_KEY_MSG_LEN);


			/*|----------------------------------------------------------------------------|
			 * |Sta Address | Key RSC | KeyID | Key Length | Temporal Key	| Rx Michael Key |
			 * |------------|---------|-------|------------|---------------|----------------|
			 |	6 bytes	 | 8 byte  |1 byte |  1 byte	|   16 bytes	|	  8 bytes	 |*/

			if (pstrWFIDrv->enuHostIFstate == HOST_IF_CONNECTED)
				memcpy(pu8keybuf, pstrWFIDrv->au8AssociatedBSSID, ETH_ALEN);
			else
				PRINT_ER("Couldn't handle WPARxGtk while enuHostIFstate is not HOST_IF_CONNECTED\n");

			memcpy(pu8keybuf + 6, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8seq, 8);

			memcpy(pu8keybuf + 14, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8keyidx, 1);

			memcpy(pu8keybuf + 15, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen, 1);
			memcpy(pu8keybuf + 16, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8key,
				    pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen);

			strWID.u16WIDid	= (u16)WID_ADD_RX_GTK;
			strWID.enuWIDtype	= WID_STR;
			strWID.ps8WidVal	= (s8 *)pu8keybuf;
			strWID.s32ValueSize = RX_MIC_KEY_MSG_LEN;

			s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
						   get_id_from_handler(pstrWFIDrv));

			kfree(pu8keybuf);

			/* ////////////////////////// */
			up(&(pstrWFIDrv->hSemTestKeyBlock));
			/* ///////////////////////// */
		}
_WPARxGtk_end_case_:
		kfree(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8key);
		kfree(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8seq);
		if (ret == -1)
			return ret;

		break;

	case WPAPtk:
		if (pstrHostIFkeyAttr->u8KeyAction & ADDKEY_AP)	{


			pu8keybuf = kmalloc(PTK_KEY_MSG_LEN + 1, GFP_KERNEL);



			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send PTK Key\n");
				ret = -1;
				goto _WPAPtk_end_case_;

			}

			/*|-----------------------------------------------------------------------------|
			 * |Station address |   keyidx     |Key Length    |Temporal Key  | Rx Michael Key |Tx Michael Key |
			 * |----------------|------------  |--------------|----------------|---------------|
			 |	6 bytes    |	1 byte    |   1byte	 |   16 bytes	 |	  8 bytes	  |	   8 bytes	  |
			 |-----------------------------------------------------------------------------|*/

			memcpy(pu8keybuf, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8macaddr, 6);  /*1 bytes Key Length */

			memcpy(pu8keybuf + 6, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8keyidx, 1);
			memcpy(pu8keybuf + 7, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen, 1);
			/*16 byte TK*/
			memcpy(pu8keybuf + 8, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8key,
				    pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen);


			strWIDList[0].u16WIDid = (u16)WID_11I_MODE;
			strWIDList[0].enuWIDtype = WID_CHAR;
			strWIDList[0].s32ValueSize = sizeof(char);
			strWIDList[0].ps8WidVal = (s8 *)(&(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Ciphermode));

			strWIDList[1].u16WIDid	= (u16)WID_ADD_PTK;
			strWIDList[1].enuWIDtype	= WID_STR;
			strWIDList[1].ps8WidVal	= (s8 *)pu8keybuf;
			strWIDList[1].s32ValueSize = PTK_KEY_MSG_LEN + 1;

			s32Error = send_config_pkt(SET_CFG, strWIDList, 2, true,
						   get_id_from_handler(pstrWFIDrv));
			kfree(pu8keybuf);

			/* ////////////////////////// */
			up(&(pstrWFIDrv->hSemTestKeyBlock));
			/* ///////////////////////// */
		}
		if (pstrHostIFkeyAttr->u8KeyAction & ADDKEY) {


			pu8keybuf = kmalloc(PTK_KEY_MSG_LEN, GFP_KERNEL);



			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send PTK Key\n");
				ret = -1;
				goto _WPAPtk_end_case_;

			}

			/*|-----------------------------------------------------------------------------|
			 * |Station address | Key Length |	Temporal Key | Rx Michael Key |Tx Michael Key |
			 * |----------------|------------|--------------|----------------|---------------|
			 |	6 bytes		 |	1byte	  |   16 bytes	 |	  8 bytes	  |	   8 bytes	  |
			 |-----------------------------------------------------------------------------|*/

			memcpy(pu8keybuf, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8macaddr, 6);  /*1 bytes Key Length */

			memcpy(pu8keybuf + 6, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen, 1);
			/*16 byte TK*/
			memcpy(pu8keybuf + 7, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8key,
				    pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen);


			strWID.u16WIDid	= (u16)WID_ADD_PTK;
			strWID.enuWIDtype	= WID_STR;
			strWID.ps8WidVal	= (s8 *)pu8keybuf;
			strWID.s32ValueSize = PTK_KEY_MSG_LEN;

			s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
						   get_id_from_handler(pstrWFIDrv));
			kfree(pu8keybuf);

			/* ////////////////////////// */
			up(&(pstrWFIDrv->hSemTestKeyBlock));
			/* ///////////////////////// */
		}

_WPAPtk_end_case_:
		kfree(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8key);
		if (ret == -1)
			return ret;

		break;


	case PMKSA:

		PRINT_D(HOSTINF_DBG, "Handling PMKSA key\n");

		pu8keybuf = kmalloc((pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFpmkidAttr.numpmkid * PMKSA_KEY_LEN) + 1, GFP_KERNEL);
		if (pu8keybuf == NULL) {
			PRINT_ER("No buffer to send PMKSA Key\n");
			return -1;
		}

		pu8keybuf[0] = pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFpmkidAttr.numpmkid;

		for (i = 0; i < pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFpmkidAttr.numpmkid; i++) {

			memcpy(pu8keybuf + ((PMKSA_KEY_LEN * i) + 1), pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFpmkidAttr.pmkidlist[i].bssid, ETH_ALEN);
			memcpy(pu8keybuf + ((PMKSA_KEY_LEN * i) + ETH_ALEN + 1), pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFpmkidAttr.pmkidlist[i].pmkid, PMKID_LEN);
		}

		strWID.u16WIDid	= (u16)WID_PMKID_INFO;
		strWID.enuWIDtype = WID_STR;
		strWID.ps8WidVal = (s8 *)pu8keybuf;
		strWID.s32ValueSize = (pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFpmkidAttr.numpmkid * PMKSA_KEY_LEN) + 1;

		s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
					   get_id_from_handler(pstrWFIDrv));

		kfree(pu8keybuf);
		break;
	}

	if (s32Error)
		PRINT_ER("Failed to send key config packet\n");


	return s32Error;
}


/**
 *  @brief Handle_Disconnect
 *  @details       Sending config packet to firmware to disconnect
 *  @param[in]    NONE
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static void Handle_Disconnect(tstrWILC_WFIDrv *drvHandler)
{
	tstrWID strWID;

	s32 s32Error = 0;
	u16 u16DummyReasonCode = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;


	strWID.u16WIDid = (u16)WID_DISCONNECT;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = (s8 *)&u16DummyReasonCode;
	strWID.s32ValueSize = sizeof(char);



	PRINT_D(HOSTINF_DBG, "Sending disconnect request\n");

	g_obtainingIP = false;
	host_int_set_power_mgmt(pstrWFIDrv, 0, 0);

	memset(u8ConnectedSSID, 0, ETH_ALEN);

	s32Error = send_config_pkt(SET_CFG, &strWID, 1, false,
				   get_id_from_handler(pstrWFIDrv));

	if (s32Error) {
		PRINT_ER("Failed to send dissconect config packet\n");
	} else {
		tstrDisconnectNotifInfo strDisconnectNotifInfo;

		memset(&strDisconnectNotifInfo, 0, sizeof(tstrDisconnectNotifInfo));

		strDisconnectNotifInfo.u16reason = 0;
		strDisconnectNotifInfo.ie = NULL;
		strDisconnectNotifInfo.ie_len = 0;

		if (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult) {
			del_timer(&pstrWFIDrv->hScanTimer);
			pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult(SCAN_EVENT_ABORTED, NULL,
									pstrWFIDrv->strWILC_UsrScanReq.u32UserScanPvoid, NULL);

			pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult = NULL;
		}

		if (pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult != NULL)	{

			/*Stop connect timer, if connection in progress*/
			if (pstrWFIDrv->enuHostIFstate == HOST_IF_WAITING_CONN_RESP) {
				PRINT_D(HOSTINF_DBG, "Upper layer requested termination of connection\n");
				del_timer(&pstrWFIDrv->hConnectTimer);
			}

			pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult(CONN_DISCONN_EVENT_DISCONN_NOTIF, NULL,
									   0, &strDisconnectNotifInfo, pstrWFIDrv->strWILC_UsrConnReq.u32UserConnectPvoid);
		} else {
			PRINT_ER("strWILC_UsrConnReq.pfUserConnectResult = NULL\n");
		}

		gbScanWhileConnected = false;

		pstrWFIDrv->enuHostIFstate = HOST_IF_IDLE;

		memset(pstrWFIDrv->au8AssociatedBSSID, 0, ETH_ALEN);


		/* Deallocation */
		pstrWFIDrv->strWILC_UsrConnReq.ssidLen = 0;
		if (pstrWFIDrv->strWILC_UsrConnReq.pu8ssid != NULL) {
			kfree(pstrWFIDrv->strWILC_UsrConnReq.pu8ssid);
			pstrWFIDrv->strWILC_UsrConnReq.pu8ssid = NULL;
		}

		if (pstrWFIDrv->strWILC_UsrConnReq.pu8bssid != NULL) {
			kfree(pstrWFIDrv->strWILC_UsrConnReq.pu8bssid);
			pstrWFIDrv->strWILC_UsrConnReq.pu8bssid = NULL;
		}

		pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen = 0;
		if (pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) {
			kfree(pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs);
			pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs = NULL;
		}


		if (gu8FlushedJoinReq != NULL && gu8FlushedJoinReqDrvHandler == drvHandler) {
			kfree(gu8FlushedJoinReq);
			gu8FlushedJoinReq = NULL;
		}
		if (gu8FlushedInfoElemAsoc != NULL && gu8FlushedJoinReqDrvHandler == drvHandler) {
			kfree(gu8FlushedInfoElemAsoc);
			gu8FlushedInfoElemAsoc = NULL;
		}

	}

	/* ////////////////////////// */
	up(&(pstrWFIDrv->hSemTestDisconnectBlock));
	/* ///////////////////////// */

}


void resolve_disconnect_aberration(tstrWILC_WFIDrv *drvHandler)
{
	tstrWILC_WFIDrv *pstrWFIDrv;

	pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;
	if (pstrWFIDrv  == NULL)
		return;
	if ((pstrWFIDrv->enuHostIFstate == HOST_IF_WAITING_CONN_RESP) || (pstrWFIDrv->enuHostIFstate == HOST_IF_CONNECTING)) {
		PRINT_D(HOSTINF_DBG, "\n\n<< correcting Supplicant state machine >>\n\n");
		host_int_disconnect(pstrWFIDrv, 1);
	}
}
static s32 Switch_Log_Terminal(tstrWILC_WFIDrv *drvHandler)
{


	s32 s32Error = 0;
	tstrWID strWID;
	static char dummy = 9;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	strWID.u16WIDid = (u16)WID_LOGTerminal_Switch;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = &dummy;
	strWID.s32ValueSize = sizeof(char);

	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));


	if (s32Error) {
		PRINT_D(HOSTINF_DBG, "Failed to switch log terminal\n");
		PRINT_ER("Failed to switch log terminal\n");
		return -EINVAL;
	}

	PRINT_INFO(HOSTINF_DBG, "MAC address set ::\n");

	return s32Error;
}

/**
 *  @brief Handle_GetChnl
 *  @details       Sending config packet to get channel
 *  @param[in]    NONE
 *  @return         NONE
 *
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_GetChnl(tstrWILC_WFIDrv *drvHandler)
{

	s32 s32Error = 0;
	tstrWID	strWID;
	/* tstrWILC_WFIDrv * pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv; */
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	strWID.u16WIDid = (u16)WID_CURRENT_CHANNEL;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = (s8 *)&gu8Chnl;
	strWID.s32ValueSize = sizeof(char);

	PRINT_D(HOSTINF_DBG, "Getting channel value\n");

	s32Error = send_config_pkt(GET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	/*get the value by searching the local copy*/
	if (s32Error) {
		PRINT_ER("Failed to get channel number\n");
		s32Error = -EFAULT;
	}

	up(&(pstrWFIDrv->hSemGetCHNL));

	return s32Error;



}


/**
 *  @brief Handle_GetRssi
 *  @details       Sending config packet to get RSSI
 *  @param[in]    NONE
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static void Handle_GetRssi(tstrWILC_WFIDrv *drvHandler)
{
	s32 s32Error = 0;
	tstrWID strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	strWID.u16WIDid = (u16)WID_RSSI;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = &gs8Rssi;
	strWID.s32ValueSize = sizeof(char);

	/*Sending Cfg*/
	PRINT_D(HOSTINF_DBG, "Getting RSSI value\n");

	s32Error = send_config_pkt(GET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error) {
		PRINT_ER("Failed to get RSSI value\n");
		s32Error = -EFAULT;
	}

	up(&(pstrWFIDrv->hSemGetRSSI));


}


static void Handle_GetLinkspeed(tstrWILC_WFIDrv *drvHandler)
{
	s32 s32Error = 0;
	tstrWID strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	gs8lnkspd = 0;

	strWID.u16WIDid = (u16)WID_LINKSPEED;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = &gs8lnkspd;
	strWID.s32ValueSize = sizeof(char);
	/*Sending Cfg*/
	PRINT_D(HOSTINF_DBG, "Getting LINKSPEED value\n");

	s32Error = send_config_pkt(GET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error) {
		PRINT_ER("Failed to get LINKSPEED value\n");
		s32Error = -EFAULT;
	}

	up(&(pstrWFIDrv->hSemGetLINKSPEED));


}

s32 Handle_GetStatistics(tstrWILC_WFIDrv *drvHandler, tstrStatistics *pstrStatistics)
{
	tstrWID strWIDList[5];
	u32 u32WidsCount = 0, s32Error = 0;

	strWIDList[u32WidsCount].u16WIDid = WID_LINKSPEED;
	strWIDList[u32WidsCount].enuWIDtype = WID_CHAR;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(char);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&(pstrStatistics->u8LinkSpeed));
	u32WidsCount++;

	strWIDList[u32WidsCount].u16WIDid = WID_RSSI;
	strWIDList[u32WidsCount].enuWIDtype = WID_CHAR;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(char);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&(pstrStatistics->s8RSSI));
	u32WidsCount++;

	strWIDList[u32WidsCount].u16WIDid = WID_SUCCESS_FRAME_COUNT;
	strWIDList[u32WidsCount].enuWIDtype = WID_INT;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(u32);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&(pstrStatistics->u32TxCount));
	u32WidsCount++;

	strWIDList[u32WidsCount].u16WIDid = WID_RECEIVED_FRAGMENT_COUNT;
	strWIDList[u32WidsCount].enuWIDtype = WID_INT;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(u32);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&(pstrStatistics->u32RxCount));
	u32WidsCount++;

	strWIDList[u32WidsCount].u16WIDid = WID_FAILED_COUNT;
	strWIDList[u32WidsCount].enuWIDtype = WID_INT;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(u32);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&(pstrStatistics->u32TxFailureCount));
	u32WidsCount++;

	s32Error = send_config_pkt(GET_CFG, strWIDList, u32WidsCount, false,
				   get_id_from_handler(drvHandler));

	if (s32Error)
		PRINT_ER("Failed to send scan paramters config packet\n");

	up(&hWaitResponse);
	return 0;

}

/**
 *  @brief Handle_Get_InActiveTime
 *  @details       Sending config packet to set mac adddress for station and
 *                 get inactive time
 *  @param[in]    NONE
 *  @return         NONE
 *
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_Get_InActiveTime(tstrWILC_WFIDrv *drvHandler,
				   struct sta_inactive_t *strHostIfStaInactiveT)
{

	s32 s32Error = 0;
	u8 *stamac;
	tstrWID	strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;


	strWID.u16WIDid = (u16)WID_SET_STA_MAC_INACTIVE_TIME;
	strWID.enuWIDtype = WID_STR;
	strWID.s32ValueSize = ETH_ALEN;
	strWID.ps8WidVal = kmalloc(strWID.s32ValueSize, GFP_KERNEL);


	stamac = strWID.ps8WidVal;
	memcpy(stamac, strHostIfStaInactiveT->mac, ETH_ALEN);


	PRINT_D(CFG80211_DBG, "SETING STA inactive time\n");


	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	/*get the value by searching the local copy*/
	if (s32Error) {
		PRINT_ER("Failed to SET incative time\n");
		return -EFAULT;
	}


	strWID.u16WIDid = (u16)WID_GET_INACTIVE_TIME;
	strWID.enuWIDtype = WID_INT;
	strWID.ps8WidVal = (s8 *)&gu32InactiveTime;
	strWID.s32ValueSize = sizeof(u32);


	s32Error = send_config_pkt(GET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	/*get the value by searching the local copy*/
	if (s32Error) {
		PRINT_ER("Failed to get incative time\n");
		return -EFAULT;
	}


	PRINT_D(CFG80211_DBG, "Getting inactive time : %d\n", gu32InactiveTime);

	up(&(pstrWFIDrv->hSemInactiveTime));

	return s32Error;



}


/**
 *  @brief Handle_AddBeacon
 *  @details       Sending config packet to add beacon
 *  @param[in]    struct beacon_attr *pstrSetBeaconParam
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static void Handle_AddBeacon(tstrWILC_WFIDrv *drvHandler,
			     struct beacon_attr *pstrSetBeaconParam)
{
	s32 s32Error = 0;
	tstrWID strWID;
	u8 *pu8CurrByte;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	PRINT_D(HOSTINF_DBG, "Adding BEACON\n");

	strWID.u16WIDid = (u16)WID_ADD_BEACON;
	strWID.enuWIDtype = WID_BIN;
	strWID.s32ValueSize = pstrSetBeaconParam->u32HeadLen + pstrSetBeaconParam->u32TailLen + 16;
	strWID.ps8WidVal = kmalloc(strWID.s32ValueSize, GFP_KERNEL);
	if (strWID.ps8WidVal == NULL)
		goto ERRORHANDLER;

	pu8CurrByte = strWID.ps8WidVal;
	*pu8CurrByte++ = (pstrSetBeaconParam->u32Interval & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->u32Interval >> 8) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->u32Interval >> 16) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->u32Interval >> 24) & 0xFF);

	*pu8CurrByte++ = (pstrSetBeaconParam->u32DTIMPeriod & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->u32DTIMPeriod >> 8) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->u32DTIMPeriod >> 16) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->u32DTIMPeriod >> 24) & 0xFF);

	*pu8CurrByte++ = (pstrSetBeaconParam->u32HeadLen & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->u32HeadLen >> 8) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->u32HeadLen >> 16) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->u32HeadLen >> 24) & 0xFF);

	memcpy(pu8CurrByte, pstrSetBeaconParam->pu8Head, pstrSetBeaconParam->u32HeadLen);
	pu8CurrByte += pstrSetBeaconParam->u32HeadLen;

	*pu8CurrByte++ = (pstrSetBeaconParam->u32TailLen & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->u32TailLen >> 8) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->u32TailLen >> 16) & 0xFF);
	*pu8CurrByte++ = ((pstrSetBeaconParam->u32TailLen >> 24) & 0xFF);

	if (pstrSetBeaconParam->pu8Tail > 0)
		memcpy(pu8CurrByte, pstrSetBeaconParam->pu8Tail, pstrSetBeaconParam->u32TailLen);
	pu8CurrByte += pstrSetBeaconParam->u32TailLen;



	/*Sending Cfg*/
	s32Error = send_config_pkt(SET_CFG, &strWID, 1, false,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error)
		PRINT_ER("Failed to send add beacon config packet\n");

ERRORHANDLER:
	kfree(strWID.ps8WidVal);
	kfree(pstrSetBeaconParam->pu8Head);
	kfree(pstrSetBeaconParam->pu8Tail);
}


/**
 *  @brief Handle_AddBeacon
 *  @details       Sending config packet to delete beacon
 *  @param[in]	tstrWILC_WFIDrv *drvHandler
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static void Handle_DelBeacon(tstrWILC_WFIDrv *drvHandler)
{
	s32 s32Error = 0;
	tstrWID strWID;
	u8 *pu8CurrByte;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	strWID.u16WIDid = (u16)WID_DEL_BEACON;
	strWID.enuWIDtype = WID_CHAR;
	strWID.s32ValueSize = sizeof(char);
	strWID.ps8WidVal = &gu8DelBcn;

	if (strWID.ps8WidVal == NULL)
		return;

	pu8CurrByte = strWID.ps8WidVal;

	PRINT_D(HOSTINF_DBG, "Deleting BEACON\n");
	/* TODO: build del beacon message*/

	/*Sending Cfg*/
	s32Error = send_config_pkt(SET_CFG, &strWID, 1, false,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error)
		PRINT_ER("Failed to send delete beacon config packet\n");
}


/**
 *  @brief WILC_HostIf_PackStaParam
 *  @details       Handling packing of the station params in a buffer
 *  @param[in]   u8* pu8Buffer, struct add_sta_param *pstrStationParam
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
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

/**
 *  @brief Handle_AddStation
 *  @details       Sending config packet to add station
 *  @param[in]   struct add_sta_param *pstrStationParam
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static void Handle_AddStation(tstrWILC_WFIDrv *drvHandler,
			      struct add_sta_param *pstrStationParam)
{
	s32 s32Error = 0;
	tstrWID strWID;
	u8 *pu8CurrByte;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	PRINT_D(HOSTINF_DBG, "Handling add station\n");
	strWID.u16WIDid = (u16)WID_ADD_STA;
	strWID.enuWIDtype = WID_BIN;
	strWID.s32ValueSize = WILC_ADD_STA_LENGTH + pstrStationParam->u8NumRates;

	strWID.ps8WidVal = kmalloc(strWID.s32ValueSize, GFP_KERNEL);
	if (strWID.ps8WidVal == NULL)
		goto ERRORHANDLER;

	pu8CurrByte = strWID.ps8WidVal;
	pu8CurrByte += WILC_HostIf_PackStaParam(pu8CurrByte, pstrStationParam);

	/*Sending Cfg*/
	s32Error = send_config_pkt(SET_CFG, &strWID, 1, false,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error != 0)
		PRINT_ER("Failed to send add station config packet\n");

ERRORHANDLER:
	kfree(pstrStationParam->pu8Rates);
	kfree(strWID.ps8WidVal);
}

/**
 *  @brief Handle_DelAllSta
 *  @details        Sending config packet to delete station
 *  @param[in]   tstrHostIFDelSta* pstrDelStaParam
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static void Handle_DelAllSta(tstrWILC_WFIDrv *drvHandler,
			     struct del_all_sta *pstrDelAllStaParam)
{
	s32 s32Error = 0;

	tstrWID strWID;
	u8 *pu8CurrByte;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;
	u8 i;
	u8 au8Zero_Buff[6] = {0};

	strWID.u16WIDid = (u16)WID_DEL_ALL_STA;
	strWID.enuWIDtype = WID_STR;
	strWID.s32ValueSize = (pstrDelAllStaParam->u8Num_AssocSta * ETH_ALEN) + 1;

	PRINT_D(HOSTINF_DBG, "Handling delete station\n");

	strWID.ps8WidVal = kmalloc((pstrDelAllStaParam->u8Num_AssocSta * ETH_ALEN) + 1, GFP_KERNEL);
	if (strWID.ps8WidVal == NULL)
		goto ERRORHANDLER;

	pu8CurrByte = strWID.ps8WidVal;

	*(pu8CurrByte++) = pstrDelAllStaParam->u8Num_AssocSta;

	for (i = 0; i < MAX_NUM_STA; i++) {
		if (memcmp(pstrDelAllStaParam->au8Sta_DelAllSta[i], au8Zero_Buff, ETH_ALEN))
			memcpy(pu8CurrByte, pstrDelAllStaParam->au8Sta_DelAllSta[i], ETH_ALEN);
		else
			continue;

		pu8CurrByte += ETH_ALEN;
	}

	/*Sending Cfg*/
	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error)
		PRINT_ER("Failed to send add station config packet\n");

ERRORHANDLER:
	kfree(strWID.ps8WidVal);

	up(&hWaitResponse);
}


/**
 *  @brief Handle_DelStation
 *  @details        Sending config packet to delete station
 *  @param[in]   struct del_sta *pstrDelStaParam
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static void Handle_DelStation(tstrWILC_WFIDrv *drvHandler,
			      struct del_sta *pstrDelStaParam)
{
	s32 s32Error = 0;
	tstrWID strWID;
	u8 *pu8CurrByte;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	strWID.u16WIDid = (u16)WID_REMOVE_STA;
	strWID.enuWIDtype = WID_BIN;
	strWID.s32ValueSize = ETH_ALEN;

	PRINT_D(HOSTINF_DBG, "Handling delete station\n");

	strWID.ps8WidVal = kmalloc(strWID.s32ValueSize, GFP_KERNEL);
	if (strWID.ps8WidVal == NULL)
		goto ERRORHANDLER;

	pu8CurrByte = strWID.ps8WidVal;

	memcpy(pu8CurrByte, pstrDelStaParam->au8MacAddr, ETH_ALEN);

	/*Sending Cfg*/
	s32Error = send_config_pkt(SET_CFG, &strWID, 1, false,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error)
		PRINT_ER("Failed to send add station config packet\n");

ERRORHANDLER:
	kfree(strWID.ps8WidVal);
}


/**
 *  @brief Handle_EditStation
 *  @details        Sending config packet to edit station
 *  @param[in]   struct add_sta_param *pstrStationParam
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static void Handle_EditStation(tstrWILC_WFIDrv *drvHandler,
			       struct add_sta_param *pstrStationParam)
{
	s32 s32Error = 0;
	tstrWID strWID;
	u8 *pu8CurrByte;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	strWID.u16WIDid = (u16)WID_EDIT_STA;
	strWID.enuWIDtype = WID_BIN;
	strWID.s32ValueSize = WILC_ADD_STA_LENGTH + pstrStationParam->u8NumRates;

	PRINT_D(HOSTINF_DBG, "Handling edit station\n");
	strWID.ps8WidVal = kmalloc(strWID.s32ValueSize, GFP_KERNEL);
	if (strWID.ps8WidVal == NULL)
		goto ERRORHANDLER;

	pu8CurrByte = strWID.ps8WidVal;
	pu8CurrByte += WILC_HostIf_PackStaParam(pu8CurrByte, pstrStationParam);

	/*Sending Cfg*/
	s32Error = send_config_pkt(SET_CFG, &strWID, 1, false,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error)
		PRINT_ER("Failed to send edit station config packet\n");

ERRORHANDLER:
	kfree(pstrStationParam->pu8Rates);
	kfree(strWID.ps8WidVal);
}

/**
 *  @brief Handle_RemainOnChan
 *  @details        Sending config packet to edit station
 *  @param[in]   tstrWILC_AddStaParam* pstrStationParam
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static int Handle_RemainOnChan(tstrWILC_WFIDrv *drvHandler,
			       struct remain_ch *pstrHostIfRemainOnChan)
{
	s32 s32Error = 0;
	u8 u8remain_on_chan_flag;
	tstrWID strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *) drvHandler;

	/*If it's a pendig remain-on-channel, don't overwrite gWFiDrvHandle values (since incoming msg is garbbage)*/
	if (!pstrWFIDrv->u8RemainOnChan_pendingreq) {
		pstrWFIDrv->strHostIfRemainOnChan.pVoid = pstrHostIfRemainOnChan->pVoid;
		pstrWFIDrv->strHostIfRemainOnChan.pRemainOnChanExpired = pstrHostIfRemainOnChan->pRemainOnChanExpired;
		pstrWFIDrv->strHostIfRemainOnChan.pRemainOnChanReady = pstrHostIfRemainOnChan->pRemainOnChanReady;
		pstrWFIDrv->strHostIfRemainOnChan.u16Channel = pstrHostIfRemainOnChan->u16Channel;
		pstrWFIDrv->strHostIfRemainOnChan.u32ListenSessionID = pstrHostIfRemainOnChan->u32ListenSessionID;
	} else {
		/*Set the channel to use it as a wid val*/
		pstrHostIfRemainOnChan->u16Channel = pstrWFIDrv->strHostIfRemainOnChan.u16Channel;
	}

	if (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult != NULL) {
		PRINT_INFO(GENERIC_DBG, "Required to remain on chan while scanning return\n");
		pstrWFIDrv->u8RemainOnChan_pendingreq = 1;
		s32Error = -EBUSY;
		goto ERRORHANDLER;
	}
	if (pstrWFIDrv->enuHostIFstate == HOST_IF_WAITING_CONN_RESP) {
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
	strWID.u16WIDid	= (u16)WID_REMAIN_ON_CHAN;
	strWID.enuWIDtype	= WID_STR;
	strWID.s32ValueSize = 2;
	strWID.ps8WidVal = kmalloc(strWID.s32ValueSize, GFP_KERNEL);

	if (strWID.ps8WidVal == NULL) {
		s32Error = -ENOMEM;
		goto ERRORHANDLER;
	}

	strWID.ps8WidVal[0] = u8remain_on_chan_flag;
	strWID.ps8WidVal[1] = (s8)pstrHostIfRemainOnChan->u16Channel;

	/*Sending Cfg*/
	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error != 0)
		PRINT_ER("Failed to set remain on channel\n");

ERRORHANDLER:
	{
		P2P_LISTEN_STATE = 1;
		pstrWFIDrv->hRemainOnChannel.data = (unsigned long)pstrWFIDrv;
		mod_timer(&pstrWFIDrv->hRemainOnChannel,
			  jiffies +
			  msecs_to_jiffies(pstrHostIfRemainOnChan->u32duration));

		/*Calling CFG ready_on_channel*/
		if (pstrWFIDrv->strHostIfRemainOnChan.pRemainOnChanReady)
			pstrWFIDrv->strHostIfRemainOnChan.pRemainOnChanReady(pstrWFIDrv->strHostIfRemainOnChan.pVoid);

		if (pstrWFIDrv->u8RemainOnChan_pendingreq)
			pstrWFIDrv->u8RemainOnChan_pendingreq = 0;
	}
	return s32Error;
}

/**
 *  @brief Handle_RegisterFrame
 *  @details
 *  @param[in]
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static int Handle_RegisterFrame(tstrWILC_WFIDrv *drvHandler,
				struct reg_frame *pstrHostIfRegisterFrame)
{
	s32 s32Error = 0;
	tstrWID strWID;
	u8 *pu8CurrByte;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	PRINT_D(HOSTINF_DBG, "Handling frame register Flag : %d FrameType: %d\n", pstrHostIfRegisterFrame->bReg, pstrHostIfRegisterFrame->u16FrameType);

	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_REGISTER_FRAME;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = kmalloc(sizeof(u16) + 2, GFP_KERNEL);
	if (strWID.ps8WidVal == NULL)
		return -ENOMEM;

	pu8CurrByte = strWID.ps8WidVal;

	*pu8CurrByte++ = pstrHostIfRegisterFrame->bReg;
	*pu8CurrByte++ = pstrHostIfRegisterFrame->u8Regid;
	memcpy(pu8CurrByte, &(pstrHostIfRegisterFrame->u16FrameType), sizeof(u16));


	strWID.s32ValueSize = sizeof(u16) + 2;


	/*Sending Cfg*/
	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error) {
		PRINT_ER("Failed to frame register config packet\n");
		s32Error = -EINVAL;
	}

	return s32Error;

}

/**
 *  @brief                      Handle_ListenStateExpired
 *  @details            Handle of listen state expiration
 *  @param[in]          NONE
 *  @return             Error code.
 *  @author
 *  @date
 *  @version		1.0
 */
#define FALSE_FRMWR_CHANNEL 100
static u32 Handle_ListenStateExpired(tstrWILC_WFIDrv *drvHandler,
				     struct remain_ch *pstrHostIfRemainOnChan)
{
	u8 u8remain_on_chan_flag;
	tstrWID strWID;
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *) drvHandler;

	PRINT_D(HOSTINF_DBG, "CANCEL REMAIN ON CHAN\n");

	/*Make sure we are already in listen state*/
	/*This is to handle duplicate expiry messages (listen timer fired and supplicant called cancel_remain_on_channel())*/
	if (P2P_LISTEN_STATE) {
		u8remain_on_chan_flag = false;
		strWID.u16WIDid	= (u16)WID_REMAIN_ON_CHAN;
		strWID.enuWIDtype	= WID_STR;
		strWID.s32ValueSize = 2;
		strWID.ps8WidVal = kmalloc(strWID.s32ValueSize, GFP_KERNEL);

		if (strWID.ps8WidVal == NULL)
			PRINT_ER("Failed to allocate memory\n");

		strWID.ps8WidVal[0] = u8remain_on_chan_flag;
		strWID.ps8WidVal[1] = FALSE_FRMWR_CHANNEL;

		/*Sending Cfg*/
		s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
					   get_id_from_handler(pstrWFIDrv));
		if (s32Error != 0) {
			PRINT_ER("Failed to set remain on channel\n");
			goto _done_;
		}

		if (pstrWFIDrv->strHostIfRemainOnChan.pRemainOnChanExpired) {
			pstrWFIDrv->strHostIfRemainOnChan.pRemainOnChanExpired(pstrWFIDrv->strHostIfRemainOnChan.pVoid
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


/**
 *  @brief                      ListenTimerCB
 *  @details            Callback function of remain-on-channel timer
 *  @param[in]          NONE
 *  @return             Error code.
 *  @author
 *  @date
 *  @version		1.0
 */
static void ListenTimerCB(unsigned long arg)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)arg;
	/*Stopping remain-on-channel timer*/
	del_timer(&pstrWFIDrv->hRemainOnChannel);

	/* prepare the Timer Callback message */
	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_LISTEN_TIMER_FIRED;
	msg.drvHandler = pstrWFIDrv;
	msg.body.remain_on_ch.u32ListenSessionID = pstrWFIDrv->strHostIfRemainOnChan.u32ListenSessionID;

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");
}

/**
 *  @brief Handle_EditStation
 *  @details        Sending config packet to edit station
 *  @param[in]   tstrWILC_AddStaParam* pstrStationParam
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static void Handle_PowerManagement(tstrWILC_WFIDrv *drvHandler,
				   struct power_mgmt_param *strPowerMgmtParam)
{
	s32 s32Error = 0;
	tstrWID strWID;
	s8 s8PowerMode;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	strWID.u16WIDid = (u16)WID_POWER_MANAGEMENT;

	if (strPowerMgmtParam->bIsEnabled == true)
		s8PowerMode = MIN_FAST_PS;
	else
		s8PowerMode = NO_POWERSAVE;
	PRINT_D(HOSTINF_DBG, "Handling power mgmt to %d\n", s8PowerMode);
	strWID.ps8WidVal = &s8PowerMode;
	strWID.s32ValueSize = sizeof(char);

	PRINT_D(HOSTINF_DBG, "Handling Power Management\n");

	/*Sending Cfg*/
	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error)
		PRINT_ER("Failed to send power management config packet\n");
}

/**
 *  @brief Handle_SetMulticastFilter
 *  @details        Set Multicast filter in firmware
 *  @param[in]   struct set_multicast *strHostIfSetMulti
 *  @return         NONE
 *  @author		asobhy
 *  @date
 *  @version	1.0
 */
static void Handle_SetMulticastFilter(tstrWILC_WFIDrv *drvHandler,
				      struct set_multicast *strHostIfSetMulti)
{
	s32 s32Error = 0;
	tstrWID strWID;
	u8 *pu8CurrByte;

	PRINT_D(HOSTINF_DBG, "Setup Multicast Filter\n");

	strWID.u16WIDid = (u16)WID_SETUP_MULTICAST_FILTER;
	strWID.enuWIDtype = WID_BIN;
	strWID.s32ValueSize = sizeof(struct set_multicast) + ((strHostIfSetMulti->u32count) * ETH_ALEN);
	strWID.ps8WidVal = kmalloc(strWID.s32ValueSize, GFP_KERNEL);
	if (strWID.ps8WidVal == NULL)
		goto ERRORHANDLER;

	pu8CurrByte = strWID.ps8WidVal;
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

	/*Sending Cfg*/
	s32Error = send_config_pkt(SET_CFG, &strWID, 1, false,
				   get_id_from_handler(drvHandler));
	if (s32Error)
		PRINT_ER("Failed to send setup multicast config packet\n");

ERRORHANDLER:
	kfree(strWID.ps8WidVal);

}


/**
 *  @brief                      Handle_AddBASession
 *  @details            Add block ack session
 *  @param[in]          tstrHostIFSetMulti* strHostIfSetMulti
 *  @return             NONE
 *  @author		Amr Abdel-Moghny
 *  @date			Feb. 2014
 *  @version		9.0
 */
static s32 Handle_AddBASession(tstrWILC_WFIDrv *drvHandler,
			       struct ba_session_info *strHostIfBASessionInfo)
{
	s32 s32Error = 0;
	tstrWID strWID;
	int AddbaTimeout = 100;
	char *ptr = NULL;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	PRINT_D(HOSTINF_DBG, "Opening Block Ack session with\nBSSID = %.2x:%.2x:%.2x\nTID=%d\nBufferSize == %d\nSessionTimeOut = %d\n",
		strHostIfBASessionInfo->au8Bssid[0],
		strHostIfBASessionInfo->au8Bssid[1],
		strHostIfBASessionInfo->au8Bssid[2],
		strHostIfBASessionInfo->u16BufferSize,
		strHostIfBASessionInfo->u16SessionTimeout,
		strHostIfBASessionInfo->u8Ted);

	strWID.u16WIDid = (u16)WID_11E_P_ACTION_REQ;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = kmalloc(BLOCK_ACK_REQ_SIZE, GFP_KERNEL);
	strWID.s32ValueSize = BLOCK_ACK_REQ_SIZE;
	ptr = strWID.ps8WidVal;
	/* *ptr++ = 0x14; */
	*ptr++ = 0x14;
	*ptr++ = 0x3;
	*ptr++ = 0x0;
	memcpy(ptr, strHostIfBASessionInfo->au8Bssid, ETH_ALEN);
	ptr += ETH_ALEN;
	*ptr++ = strHostIfBASessionInfo->u8Ted;
	/* BA Policy*/
	*ptr++ = 1;
	/* Buffer size*/
	*ptr++ = (strHostIfBASessionInfo->u16BufferSize & 0xFF);
	*ptr++ = ((strHostIfBASessionInfo->u16BufferSize >> 16) & 0xFF);
	/* BA timeout*/
	*ptr++ = (strHostIfBASessionInfo->u16SessionTimeout & 0xFF);
	*ptr++ = ((strHostIfBASessionInfo->u16SessionTimeout >> 16) & 0xFF);
	/* ADDBA timeout*/
	*ptr++ = (AddbaTimeout & 0xFF);
	*ptr++ = ((AddbaTimeout >> 16) & 0xFF);
	/* Group Buffer Max Frames*/
	*ptr++ = 8;
	/* Group Buffer Timeout */
	*ptr++ = 0;

	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error)
		PRINT_D(HOSTINF_DBG, "Couldn't open BA Session\n");


	strWID.u16WIDid = (u16)WID_11E_P_ACTION_REQ;
	strWID.enuWIDtype = WID_STR;
	strWID.s32ValueSize = 15;
	ptr = strWID.ps8WidVal;
	/* *ptr++ = 0x14; */
	*ptr++ = 15;
	*ptr++ = 7;
	*ptr++ = 0x2;
	memcpy(ptr, strHostIfBASessionInfo->au8Bssid, ETH_ALEN);
	ptr += ETH_ALEN;
	/* TID*/
	*ptr++ = strHostIfBASessionInfo->u8Ted;
	/* Max Num MSDU */
	*ptr++ = 8;
	/* BA timeout*/
	*ptr++ = (strHostIfBASessionInfo->u16BufferSize & 0xFF);
	*ptr++ = ((strHostIfBASessionInfo->u16SessionTimeout >> 16) & 0xFF);
	/*Ack-Policy */
	*ptr++ = 3;
	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));

	if (strWID.ps8WidVal != NULL)
		kfree(strWID.ps8WidVal);

	return s32Error;

}


/**
 *  @brief                      Handle_DelBASession
 *  @details            Delete block ack session
 *  @param[in]          tstrHostIFSetMulti* strHostIfSetMulti
 *  @return             NONE
 *  @author		Amr Abdel-Moghny
 *  @date			Feb. 2013
 *  @version		9.0
 */
static s32 Handle_DelBASession(tstrWILC_WFIDrv *drvHandler,
			       struct ba_session_info *strHostIfBASessionInfo)
{
	s32 s32Error = 0;
	tstrWID strWID;
	char *ptr = NULL;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	PRINT_D(GENERIC_DBG, "Delete Block Ack session with\nBSSID = %.2x:%.2x:%.2x\nTID=%d\n",
		strHostIfBASessionInfo->au8Bssid[0],
		strHostIfBASessionInfo->au8Bssid[1],
		strHostIfBASessionInfo->au8Bssid[2],
		strHostIfBASessionInfo->u8Ted);

	strWID.u16WIDid = (u16)WID_11E_P_ACTION_REQ;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = kmalloc(BLOCK_ACK_REQ_SIZE, GFP_KERNEL);
	strWID.s32ValueSize = BLOCK_ACK_REQ_SIZE;
	ptr = strWID.ps8WidVal;
	/* *ptr++ = 0x14; */
	*ptr++ = 0x14;
	*ptr++ = 0x3;
	*ptr++ = 0x2;
	memcpy(ptr, strHostIfBASessionInfo->au8Bssid, ETH_ALEN);
	ptr += ETH_ALEN;
	*ptr++ = strHostIfBASessionInfo->u8Ted;
	/* BA direction = recipent*/
	*ptr++ = 0;
	/* Delba Reason */
	*ptr++ = 32; /* Unspecific QOS reason */

	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error)
		PRINT_D(HOSTINF_DBG, "Couldn't delete BA Session\n");


	strWID.u16WIDid = (u16)WID_11E_P_ACTION_REQ;
	strWID.enuWIDtype = WID_STR;
	strWID.s32ValueSize = 15;
	ptr = strWID.ps8WidVal;
	/* *ptr++ = 0x14; */
	*ptr++ = 15;
	*ptr++ = 7;
	*ptr++ = 0x3;
	memcpy(ptr, strHostIfBASessionInfo->au8Bssid, ETH_ALEN);
	ptr += ETH_ALEN;
	/* TID*/
	*ptr++ = strHostIfBASessionInfo->u8Ted;

	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));

	if (strWID.ps8WidVal != NULL)
		kfree(strWID.ps8WidVal);

	up(&hWaitResponse);

	return s32Error;

}


/**
 *  @brief                      Handle_DelAllRxBASessions
 *  @details            Delete all Rx BA sessions
 *  @param[in]          tstrHostIFSetMulti* strHostIfSetMulti
 *  @return             NONE
 *  @author		Abdelrahman Sobhy
 *  @date			Feb. 2013
 *  @version		9.0
 */
static s32 Handle_DelAllRxBASessions(tstrWILC_WFIDrv *drvHandler,
				     struct ba_session_info *strHostIfBASessionInfo)
{
	s32 s32Error = 0;
	tstrWID strWID;
	char *ptr = NULL;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	PRINT_D(GENERIC_DBG, "Delete Block Ack session with\nBSSID = %.2x:%.2x:%.2x\nTID=%d\n",
		strHostIfBASessionInfo->au8Bssid[0],
		strHostIfBASessionInfo->au8Bssid[1],
		strHostIfBASessionInfo->au8Bssid[2],
		strHostIfBASessionInfo->u8Ted);

	strWID.u16WIDid = (u16)WID_DEL_ALL_RX_BA;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = kmalloc(BLOCK_ACK_REQ_SIZE, GFP_KERNEL);
	strWID.s32ValueSize = BLOCK_ACK_REQ_SIZE;
	ptr = strWID.ps8WidVal;
	*ptr++ = 0x14;
	*ptr++ = 0x3;
	*ptr++ = 0x2;
	memcpy(ptr, strHostIfBASessionInfo->au8Bssid, ETH_ALEN);
	ptr += ETH_ALEN;
	*ptr++ = strHostIfBASessionInfo->u8Ted;
	/* BA direction = recipent*/
	*ptr++ = 0;
	/* Delba Reason */
	*ptr++ = 32; /* Unspecific QOS reason */

	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error)
		PRINT_D(HOSTINF_DBG, "Couldn't delete BA Session\n");


	if (strWID.ps8WidVal != NULL)
		kfree(strWID.ps8WidVal);

	up(&hWaitResponse);

	return s32Error;

}

/**
 *  @brief hostIFthread
 *  @details        Main thread to handle message queue requests
 *  @param[in]   void* pvArg
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static int hostIFthread(void *pvArg)
{
	u32 u32Ret;
	struct host_if_msg msg;
	tstrWILC_WFIDrv *pstrWFIDrv;

	memset(&msg, 0, sizeof(struct host_if_msg));

	while (1) {
		wilc_mq_recv(&gMsgQHostIF, &msg, sizeof(struct host_if_msg), &u32Ret);
		pstrWFIDrv = (tstrWILC_WFIDrv *)msg.drvHandler;
		if (msg.id == HOST_IF_MSG_EXIT) {
			PRINT_D(GENERIC_DBG, "THREAD: Exiting HostIfThread\n");
			break;
		}


		/*Re-Queue HIF message*/
		if ((!g_wilc_initialized)) {
			PRINT_D(GENERIC_DBG, "--WAIT--");
			usleep_range(200 * 1000, 200 * 1000);
			wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
			continue;
		}

		if (msg.id == HOST_IF_MSG_CONNECT && pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult != NULL) {
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
			Handle_Scan(msg.drvHandler, &msg.body.scan_info);
			break;

		case HOST_IF_MSG_CONNECT:
			Handle_Connect(msg.drvHandler, &msg.body.con_info);
			break;

		case HOST_IF_MSG_FLUSH_CONNECT:
			Handle_FlushConnect(msg.drvHandler);
			break;

		case HOST_IF_MSG_RCVD_NTWRK_INFO:
			Handle_RcvdNtwrkInfo(msg.drvHandler, &msg.body.net_info);
			break;

		case HOST_IF_MSG_RCVD_GNRL_ASYNC_INFO:
			Handle_RcvdGnrlAsyncInfo(msg.drvHandler, &msg.body.async_info);
			break;

		case HOST_IF_MSG_KEY:
			Handle_Key(msg.drvHandler, &msg.body.key_info);
			break;

		case HOST_IF_MSG_CFG_PARAMS:

			Handle_CfgParam(msg.drvHandler, &msg.body.cfg_info);
			break;

		case HOST_IF_MSG_SET_CHANNEL:
			Handle_SetChannel(msg.drvHandler, &msg.body.channel_info);
			break;

		case HOST_IF_MSG_DISCONNECT:
			Handle_Disconnect(msg.drvHandler);
			break;

		case HOST_IF_MSG_RCVD_SCAN_COMPLETE:
			del_timer(&pstrWFIDrv->hScanTimer);
			PRINT_D(HOSTINF_DBG, "scan completed successfully\n");

			/*Allow chip sleep, only if both interfaces are not connected*/
			if (!linux_wlan_get_num_conn_ifcs())
				chip_sleep_manually(INFINITE_SLEEP_TIME);

			Handle_ScanDone(msg.drvHandler, SCAN_EVENT_DONE);

			if (pstrWFIDrv->u8RemainOnChan_pendingreq)
				Handle_RemainOnChan(msg.drvHandler, &msg.body.remain_on_ch);

			break;

		case HOST_IF_MSG_GET_RSSI:
			Handle_GetRssi(msg.drvHandler);
			break;

		case HOST_IF_MSG_GET_LINKSPEED:
			Handle_GetLinkspeed(msg.drvHandler);
			break;

		case HOST_IF_MSG_GET_STATISTICS:
			Handle_GetStatistics(msg.drvHandler, (tstrStatistics *)msg.body.pUserData);
			break;

		case HOST_IF_MSG_GET_CHNL:
			Handle_GetChnl(msg.drvHandler);
			break;

		case HOST_IF_MSG_ADD_BEACON:
			Handle_AddBeacon(msg.drvHandler, &msg.body.beacon_info);
			break;

		case HOST_IF_MSG_DEL_BEACON:
			Handle_DelBeacon(msg.drvHandler);
			break;

		case HOST_IF_MSG_ADD_STATION:
			Handle_AddStation(msg.drvHandler, &msg.body.add_sta_info);
			break;

		case HOST_IF_MSG_DEL_STATION:
			Handle_DelStation(msg.drvHandler, &msg.body.del_sta_info);
			break;

		case HOST_IF_MSG_EDIT_STATION:
			Handle_EditStation(msg.drvHandler, &msg.body.edit_sta_info);
			break;

		case HOST_IF_MSG_GET_INACTIVETIME:
			Handle_Get_InActiveTime(msg.drvHandler, &msg.body.mac_info);
			break;

		case HOST_IF_MSG_SCAN_TIMER_FIRED:
			PRINT_D(HOSTINF_DBG, "Scan Timeout\n");

			Handle_ScanDone(msg.drvHandler, SCAN_EVENT_ABORTED);
			break;

		case HOST_IF_MSG_CONNECT_TIMER_FIRED:
			PRINT_D(HOSTINF_DBG, "Connect Timeout\n");
			Handle_ConnectTimeout(msg.drvHandler);
			break;

		case HOST_IF_MSG_POWER_MGMT:
			Handle_PowerManagement(msg.drvHandler, &msg.body.pwr_mgmt_info);
			break;

		case HOST_IF_MSG_SET_WFIDRV_HANDLER:
			Handle_SetWfiDrvHandler(msg.drvHandler,
						&msg.body.drv);
			break;

		case HOST_IF_MSG_SET_OPERATION_MODE:
			Handle_SetOperationMode(msg.drvHandler, &msg.body.mode);
			break;

		case HOST_IF_MSG_SET_IPADDRESS:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_SET_IPADDRESS\n");
			Handle_set_IPAddress(msg.drvHandler, msg.body.ip_info.au8IPAddr, msg.body.ip_info.idx);
			break;

		case HOST_IF_MSG_GET_IPADDRESS:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_SET_IPADDRESS\n");
			Handle_get_IPAddress(msg.drvHandler, msg.body.ip_info.au8IPAddr, msg.body.ip_info.idx);
			break;

		case HOST_IF_MSG_SET_MAC_ADDRESS:
			Handle_SetMacAddress(msg.drvHandler, &msg.body.set_mac_info);
			break;

		case HOST_IF_MSG_GET_MAC_ADDRESS:
			Handle_GetMacAddress(msg.drvHandler, &msg.body.get_mac_info);
			break;

		case HOST_IF_MSG_REMAIN_ON_CHAN:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_REMAIN_ON_CHAN\n");
			Handle_RemainOnChan(msg.drvHandler, &msg.body.remain_on_ch);
			break;

		case HOST_IF_MSG_REGISTER_FRAME:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_REGISTER_FRAME\n");
			Handle_RegisterFrame(msg.drvHandler, &msg.body.strHostIfRegisterFrame);
			break;

		case HOST_IF_MSG_LISTEN_TIMER_FIRED:
			Handle_ListenStateExpired(msg.drvHandler, &msg.body.remain_on_ch);
			break;

		case HOST_IF_MSG_SET_MULTICAST_FILTER:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_SET_MULTICAST_FILTER\n");
			Handle_SetMulticastFilter(msg.drvHandler, &msg.body.multicast_info);
			break;

		case HOST_IF_MSG_ADD_BA_SESSION:
			Handle_AddBASession(msg.drvHandler, &msg.body.session_info);
			break;

		case HOST_IF_MSG_DEL_ALL_RX_BA_SESSIONS:
			Handle_DelAllRxBASessions(msg.drvHandler, &msg.body.session_info);
			break;

		case HOST_IF_MSG_DEL_ALL_STA:
			Handle_DelAllSta(msg.drvHandler, &msg.body.strHostIFDelAllSta);
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

	/* prepare the Timer Callback message */
	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.drvHandler = pvArg;
	msg.id = HOST_IF_MSG_SCAN_TIMER_FIRED;

	/* send the message */
	wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
}

static void TimerCB_Connect(unsigned long arg)
{
	void *pvArg = (void *)arg;
	struct host_if_msg msg;

	/* prepare the Timer Callback message */
	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.drvHandler = pvArg;
	msg.id = HOST_IF_MSG_CONNECT_TIMER_FIRED;

	/* send the message */
	wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
}


/**
 *  @brief              removes wpa/wpa2 keys
 *  @details    only in BSS STA mode if External Supplicant support is enabled.
 *                              removes all WPA/WPA2 station key entries from MAC hardware.
 *  @param[in,out] handle to the wifi driver
 *  @param[in]  6 bytes of Station Adress in the station entry table
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
/* Check implementation in core adding 9 bytes to the input! */
s32 host_int_remove_key(tstrWILC_WFIDrv *hWFIDrv, const u8 *pu8StaAddress)
{
	s32 s32Error = 0;
	tstrWID strWID;
	/* tstrWILC_WFIDrv * pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv; */

	strWID.u16WIDid	= (u16)WID_REMOVE_KEY;
	strWID.enuWIDtype	= WID_STR;
	strWID.ps8WidVal	= (s8 *)pu8StaAddress;
	strWID.s32ValueSize = 6;

	return s32Error;

}

/**
 *  @brief              removes WEP key
 *  @details    valid only in BSS STA mode if External Supplicant support is enabled.
 *                              remove a WEP key entry from MAC HW.
 *                              The BSS Station automatically finds the index of the entry using its
 *                              BSS ID and removes that entry from the MAC hardware.
 *  @param[in,out] handle to the wifi driver
 *  @param[in]  6 bytes of Station Adress in the station entry table
 *  @return             Error code indicating success/failure
 *  @note               NO need for the STA add since it is not used for processing
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_remove_wep_key(tstrWILC_WFIDrv *hWFIDrv, u8 u8keyIdx)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;


	if (pstrWFIDrv == NULL) {
		s32Error = -EFAULT;
		PRINT_ER("Failed to send setup multicast config packet\n");
		return s32Error;
	}

	/* prepare the Remove Wep Key Message */
	memset(&msg, 0, sizeof(struct host_if_msg));


	msg.id = HOST_IF_MSG_KEY;
	msg.body.key_info.enuKeyType = WEP;
	msg.body.key_info.u8KeyAction = REMOVEKEY;
	msg.drvHandler = hWFIDrv;



	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx = u8keyIdx;

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Error in sending message queue : Request to remove WEP key\n");
	down(&(pstrWFIDrv->hSemTestKeyBlock));

	return s32Error;
}

/**
 *  @brief              sets WEP default key
 *  @details    Sets the index of the WEP encryption key in use,
 *                              in the key table
 *  @param[in,out] handle to the wifi driver
 *  @param[in]  key index ( 0, 1, 2, 3)
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_set_WEPDefaultKeyID(tstrWILC_WFIDrv *hWFIDrv, u8 u8Index)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;


	if (pstrWFIDrv == NULL) {
		s32Error = -EFAULT;
		PRINT_ER("driver is null\n");
		return s32Error;
	}

	/* prepare the Key Message */
	memset(&msg, 0, sizeof(struct host_if_msg));


	msg.id = HOST_IF_MSG_KEY;
	msg.body.key_info.enuKeyType = WEP;
	msg.body.key_info.u8KeyAction = DEFAULTKEY;
	msg.drvHandler = hWFIDrv;


	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx = u8Index;

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Error in sending message queue : Default key index\n");
	down(&(pstrWFIDrv->hSemTestKeyBlock));

	return s32Error;
}

/**
 *  @brief              sets WEP deafault key
 *  @details    valid only in BSS STA mode if External Supplicant support is enabled.
 *                              sets WEP key entry into MAC hardware when it receives the
 *                              corresponding request from NDIS.
 *  @param[in,out] handle to the wifi driver
 *  @param[in]  message containing WEP Key in the following format
 *|---------------------------------------|
 *|Key ID Value | Key Length |	Key		|
 *|-------------|------------|------------|
 |	1byte	  |		1byte  | Key Length	|
 ||---------------------------------------|
 |
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_add_wep_key_bss_sta(tstrWILC_WFIDrv *hWFIDrv, const u8 *pu8WepKey, u8 u8WepKeylen, u8 u8Keyidx)
{

	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;

	if (pstrWFIDrv == NULL) {
		s32Error = -EFAULT;
		PRINT_ER("driver is null\n");
		return s32Error;
	}

	/* prepare the Key Message */
	memset(&msg, 0, sizeof(struct host_if_msg));


	msg.id = HOST_IF_MSG_KEY;
	msg.body.key_info.enuKeyType = WEP;
	msg.body.key_info.u8KeyAction = ADDKEY;
	msg.drvHandler = hWFIDrv;


	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey = kmalloc(u8WepKeylen, GFP_KERNEL);

	memcpy(msg.body.key_info.uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey,
		    pu8WepKey, u8WepKeylen);


	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen = (u8WepKeylen);

	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx = u8Keyidx;

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Error in sending message queue :WEP Key\n");
	down(&(pstrWFIDrv->hSemTestKeyBlock));

	return s32Error;

}

/**
 *
 *  @brief              host_int_add_wep_key_bss_ap
 *  @details    valid only in BSS AP mode if External Supplicant support is enabled.
 *                              sets WEP key entry into MAC hardware when it receives the
 *
 *                              corresponding request from NDIS.
 *  @param[in,out] handle to the wifi driver
 *
 *
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		mdaftedar
 *  @date		28 FEB 2013
 *  @version		1.0
 */
s32 host_int_add_wep_key_bss_ap(tstrWILC_WFIDrv *hWFIDrv, const u8 *pu8WepKey, u8 u8WepKeylen, u8 u8Keyidx, u8 u8mode, AUTHTYPE_T tenuAuth_type)
{

	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	u8 i;

	if (pstrWFIDrv == NULL) {
		s32Error = -EFAULT;
		PRINT_ER("driver is null\n");
		return s32Error;
	}

	/* prepare the Key Message */
	memset(&msg, 0, sizeof(struct host_if_msg));

	if (INFO) {
		for (i = 0; i < u8WepKeylen; i++)
			PRINT_INFO(HOSTAPD_DBG, "KEY is %x\n", pu8WepKey[i]);
	}
	msg.id = HOST_IF_MSG_KEY;
	msg.body.key_info.enuKeyType = WEP;
	msg.body.key_info.u8KeyAction = ADDKEY_AP;
	msg.drvHandler = hWFIDrv;


	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey = kmalloc(u8WepKeylen, GFP_KERNEL);


	memcpy(msg.body.key_info.uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey,
		    pu8WepKey, (u8WepKeylen));


	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen = (u8WepKeylen);

	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx = u8Keyidx;

	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwepAttr.u8mode = u8mode;

	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwepAttr.tenuAuth_type = tenuAuth_type;
	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));

	if (s32Error)
		PRINT_ER("Error in sending message queue :WEP Key\n");
	down(&(pstrWFIDrv->hSemTestKeyBlock));

	return s32Error;

}

/**
 *  @brief              adds ptk Key
 *  @details
 *  @param[in,out] handle to the wifi driver
 *  @param[in]  message containing PTK Key in the following format
 *|-----------------------------------------------------------------------------|
 *|Station address | Key Length |	Temporal Key | Rx Michael Key |Tx Michael Key |
 *|----------------|------------|--------------|----------------|---------------|
 |	6 bytes		 |	1byte	  |   16 bytes	 |	  8 bytes	  |	   8 bytes	  |
 ||-----------------------------------------------------------------------------|
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_add_ptk(tstrWILC_WFIDrv *hWFIDrv, const u8 *pu8Ptk, u8 u8PtkKeylen,
			     const u8 *mac_addr, const u8 *pu8RxMic, const u8 *pu8TxMic, u8 mode, u8 u8Ciphermode, u8 u8Idx)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	u8 u8KeyLen = u8PtkKeylen;
	u32 i;

	if (pstrWFIDrv == NULL) {
		s32Error = -EFAULT;
		PRINT_ER("driver is null\n");
		return s32Error;
	}
	if (pu8RxMic != NULL)
		u8KeyLen += RX_MIC_KEY_LEN;
	if (pu8TxMic != NULL)
		u8KeyLen += TX_MIC_KEY_LEN;

	/* prepare the Key Message */
	memset(&msg, 0, sizeof(struct host_if_msg));


	msg.id = HOST_IF_MSG_KEY;
	msg.body.key_info.enuKeyType = WPAPtk;
	if (mode == AP_MODE) {
		msg.body.key_info.u8KeyAction = ADDKEY_AP;
		msg.body.key_info.
		uniHostIFkeyAttr.strHostIFwpaAttr.u8keyidx = u8Idx;
	}
	if (mode == STATION_MODE)
		msg.body.key_info.u8KeyAction = ADDKEY;


	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwpaAttr.pu8key = kmalloc(u8PtkKeylen, GFP_KERNEL);


	memcpy(msg.body.key_info.uniHostIFkeyAttr.strHostIFwpaAttr.pu8key,
		    pu8Ptk, u8PtkKeylen);

	if (pu8RxMic != NULL) {

		memcpy(msg.body.key_info.uniHostIFkeyAttr.strHostIFwpaAttr.pu8key + 16,
			    pu8RxMic, RX_MIC_KEY_LEN);
		if (INFO) {
			for (i = 0; i < RX_MIC_KEY_LEN; i++)
				PRINT_INFO(CFG80211_DBG, "PairwiseRx[%d] = %x\n", i, pu8RxMic[i]);
		}
	}
	if (pu8TxMic != NULL) {

		memcpy(msg.body.key_info.uniHostIFkeyAttr.strHostIFwpaAttr.pu8key + 24,
			    pu8TxMic, TX_MIC_KEY_LEN);
		if (INFO) {
			for (i = 0; i < TX_MIC_KEY_LEN; i++)
				PRINT_INFO(CFG80211_DBG, "PairwiseTx[%d] = %x\n", i, pu8TxMic[i]);
		}
	}

	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen = u8KeyLen;

	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwpaAttr.u8Ciphermode = u8Ciphermode;
	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwpaAttr.pu8macaddr = mac_addr;
	msg.drvHandler = hWFIDrv;

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));

	if (s32Error)
		PRINT_ER("Error in sending message queue:  PTK Key\n");

	/* ////////////// */
	down(&(pstrWFIDrv->hSemTestKeyBlock));
	/* /////// */

	return s32Error;
}

/**
 *  @brief              adds Rx GTk Key
 *  @details
 *  @param[in,out] handle to the wifi driver
 *  @param[in]  pu8RxGtk : contains temporal key | Rx Mic | Tx Mic
 *                              u8GtkKeylen :The total key length
 *
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_add_rx_gtk(tstrWILC_WFIDrv *hWFIDrv, const u8 *pu8RxGtk, u8 u8GtkKeylen,
				u8 u8KeyIdx, u32 u32KeyRSClen, const u8 *KeyRSC,
				const u8 *pu8RxMic, const u8 *pu8TxMic, u8 mode, u8 u8Ciphermode)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	u8 u8KeyLen = u8GtkKeylen;

	if (pstrWFIDrv == NULL) {
		s32Error = -EFAULT;
		PRINT_ER("driver is null\n");
		return s32Error;
	}
	/* prepare the Key Message */
	memset(&msg, 0, sizeof(struct host_if_msg));


	if (pu8RxMic != NULL)
		u8KeyLen += RX_MIC_KEY_LEN;
	if (pu8TxMic != NULL)
		u8KeyLen += TX_MIC_KEY_LEN;
	if (KeyRSC != NULL) {
		msg.body.key_info.
		uniHostIFkeyAttr.strHostIFwpaAttr.pu8seq = kmalloc(u32KeyRSClen, GFP_KERNEL);

		memcpy(msg.body.key_info.uniHostIFkeyAttr.strHostIFwpaAttr.pu8seq,
			    KeyRSC, u32KeyRSClen);
	}


	msg.id = HOST_IF_MSG_KEY;
	msg.body.key_info.enuKeyType = WPARxGtk;
	msg.drvHandler = hWFIDrv;

	if (mode == AP_MODE) {
		msg.body.key_info.u8KeyAction = ADDKEY_AP;
		msg.body.key_info.uniHostIFkeyAttr.strHostIFwpaAttr.u8Ciphermode = u8Ciphermode;
	}
	if (mode == STATION_MODE)
		msg.body.key_info.u8KeyAction = ADDKEY;


	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwpaAttr.pu8key = kmalloc(u8KeyLen, GFP_KERNEL);

	memcpy(msg.body.key_info.uniHostIFkeyAttr.strHostIFwpaAttr.pu8key,
		    pu8RxGtk, u8GtkKeylen);

	if (pu8RxMic != NULL) {

		memcpy(msg.body.key_info.uniHostIFkeyAttr.strHostIFwpaAttr.pu8key + 16,
			    pu8RxMic, RX_MIC_KEY_LEN);

	}
	if (pu8TxMic != NULL) {

		memcpy(msg.body.key_info.uniHostIFkeyAttr.strHostIFwpaAttr.pu8key + 24,
			    pu8TxMic, TX_MIC_KEY_LEN);

	}

	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwpaAttr.u8keyidx = u8KeyIdx;
	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen = u8KeyLen;

	msg.body.key_info.
	uniHostIFkeyAttr.strHostIFwpaAttr.u8seqlen = u32KeyRSClen;



	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Error in sending message queue:  RX GTK\n");
	/* ////////////// */
	down(&(pstrWFIDrv->hSemTestKeyBlock));
	/* /////// */

	return s32Error;
}

/**
 *  @brief              host_int_set_pmkid_info
 *  @details    caches the pmkid valid only in BSS STA mode if External Supplicant
 *                              support is enabled. This Function sets the PMKID in firmware
 *                              when host drivr receives the corresponding request from NDIS.
 *                              The firmware then includes theset PMKID in the appropriate
 *                              management frames
 *  @param[in,out] handle to the wifi driver
 *  @param[in]  message containing PMKID Info in the following format
 *|-----------------------------------------------------------------|
 *|NumEntries |	BSSID[1] | PMKID[1] |  ...	| BSSID[K] | PMKID[K] |
 *|-----------|------------|----------|-------|----------|----------|
 |	   1	|		6	 |   16		|  ...	|	 6	   |	16	  |
 ||-----------------------------------------------------------------|
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_set_pmkid_info(tstrWILC_WFIDrv *hWFIDrv, tstrHostIFpmkidAttr *pu8PmkidInfoArray)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	u32 i;


	if (pstrWFIDrv == NULL) {
		s32Error = -EFAULT;
		PRINT_ER("driver is null\n");
		return s32Error;
	}

	/* prepare the Key Message */
	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_KEY;
	msg.body.key_info.enuKeyType = PMKSA;
	msg.body.key_info.u8KeyAction = ADDKEY;
	msg.drvHandler = hWFIDrv;

	for (i = 0; i < pu8PmkidInfoArray->numpmkid; i++) {

		memcpy(msg.body.key_info.uniHostIFkeyAttr.strHostIFpmkidAttr.pmkidlist[i].bssid, &pu8PmkidInfoArray->pmkidlist[i].bssid,
			    ETH_ALEN);

		memcpy(msg.body.key_info.uniHostIFkeyAttr.strHostIFpmkidAttr.pmkidlist[i].pmkid, &pu8PmkidInfoArray->pmkidlist[i].pmkid,
			    PMKID_LEN);
	}

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER(" Error in sending messagequeue: PMKID Info\n");

	return s32Error;
}

/**
 *  @brief              gets the cached the pmkid info
 *  @details    valid only in BSS STA mode if External Supplicant
 *                              support is enabled. This Function sets the PMKID in firmware
 *                              when host drivr receives the corresponding request from NDIS.
 *                              The firmware then includes theset PMKID in the appropriate
 *                              management frames
 *  @param[in,out] handle to the wifi driver,
 *                                message containing PMKID Info in the following format
 *|-----------------------------------------------------------------|
 *|NumEntries |	BSSID[1] | PMKID[1] |  ...	| BSSID[K] | PMKID[K] |
 *|-----------|------------|----------|-------|----------|----------|
 |	   1	|		6	 |   16		|  ...	|	 6	   |	16	  |
 ||-----------------------------------------------------------------|
 *  @param[in]
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_get_pmkid_info(tstrWILC_WFIDrv *hWFIDrv, u8 *pu8PmkidInfoArray,
				    u32 u32PmkidInfoLen)
{
	s32 s32Error = 0;
	tstrWID strWID;
	/* tstrWILC_WFIDrv * pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv; */

	strWID.u16WIDid	= (u16)WID_PMKID_INFO;
	strWID.enuWIDtype	= WID_STR;
	strWID.s32ValueSize = u32PmkidInfoLen;
	strWID.ps8WidVal = pu8PmkidInfoArray;

	return s32Error;
}

/**
 *  @brief              sets the pass phrase
 *  @details    AP/STA mode. This function gives the pass phrase used to
 *                              generate the Pre-Shared Key when WPA/WPA2 is enabled
 *                              The length of the field can vary from 8 to 64 bytes,
 *                              the lower layer should get the
 *  @param[in,out] handle to the wifi driver,
 *  @param[in]   String containing PSK
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_set_RSNAConfigPSKPassPhrase(tstrWILC_WFIDrv *hWFIDrv, u8 *pu8PassPhrase,
						 u8 u8Psklength)
{
	s32 s32Error = 0;
	tstrWID strWID;

	/*validating psk length*/
	if ((u8Psklength > 7) && (u8Psklength < 65)) {
		strWID.u16WIDid	= (u16)WID_11I_PSK;
		strWID.enuWIDtype	= WID_STR;
		strWID.ps8WidVal	= pu8PassPhrase;
		strWID.s32ValueSize = u8Psklength;
	}

	return s32Error;
}
/**
 *  @brief              host_int_get_MacAddress
 *  @details	gets mac address
 *  @param[in,out] handle to the wifi driver,
 *
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		mdaftedar
 *  @date		19 April 2012
 *  @version		1.0
 */
s32 host_int_get_MacAddress(tstrWILC_WFIDrv *hWFIDrv, u8 *pu8MacAddress)
{
	s32 s32Error = 0;
	struct host_if_msg msg;


	/* prepare the Message */
	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_GET_MAC_ADDRESS;
	msg.body.get_mac_info.u8MacAddress = pu8MacAddress;
	msg.drvHandler = hWFIDrv;
	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("Failed to send get mac address\n");
		return -EFAULT;
	}

	down(&hWaitResponse);
	return s32Error;
}

/**
 *  @brief              host_int_set_MacAddress
 *  @details	sets mac address
 *  @param[in,out] handle to the wifi driver,
 *
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		mabubakr
 *  @date		16 July 2012
 *  @version		1.0
 */
s32 host_int_set_MacAddress(tstrWILC_WFIDrv *hWFIDrv, u8 *pu8MacAddress)
{
	s32 s32Error = 0;
	struct host_if_msg msg;

	PRINT_D(GENERIC_DBG, "mac addr = %x:%x:%x\n", pu8MacAddress[0], pu8MacAddress[1], pu8MacAddress[2]);

	/* prepare setting mac address message */
	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_SET_MAC_ADDRESS;
	memcpy(msg.body.set_mac_info.u8MacAddress, pu8MacAddress, ETH_ALEN);
	msg.drvHandler = hWFIDrv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Failed to send message queue: Set mac address\n");

	return s32Error;

}

/**
 *  @brief              host_int_get_RSNAConfigPSKPassPhrase
 *  @details    gets the pass phrase:AP/STA mode. This function gets the pass phrase used to
 *                              generate the Pre-Shared Key when WPA/WPA2 is enabled
 *                              The length of the field can vary from 8 to 64 bytes,
 *                              the lower layer should get the
 *  @param[in,out] handle to the wifi driver,
 *                                String containing PSK
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_get_RSNAConfigPSKPassPhrase(tstrWILC_WFIDrv *hWFIDrv,
						 u8 *pu8PassPhrase, u8 u8Psklength)
{
	s32 s32Error = 0;
	tstrWID strWID;
	/* tstrWILC_WFIDrv * pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv; */

	strWID.u16WIDid	= (u16)WID_11I_PSK;
	strWID.enuWIDtype	= WID_STR;
	strWID.s32ValueSize = u8Psklength;
	strWID.ps8WidVal	= pu8PassPhrase;

	return s32Error;
}

/**
 *  @brief              sets a start scan request
 *  @details
 *  @param[in,out] handle to the wifi driver,
 *  @param[in]	Scan Source one of the following values
 *                              DEFAULT_SCAN        0
 *                              USER_SCAN           BIT0
 *                              OBSS_PERIODIC_SCAN  BIT1
 *                              OBSS_ONETIME_SCAN   BIT2
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_set_start_scan_req(tstrWILC_WFIDrv *hWFIDrv, u8 scanSource)
{
	s32 s32Error = 0;
	tstrWID strWID;
	/* tstrWILC_WFIDrv * pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv; */

	strWID.u16WIDid = (u16)WID_START_SCAN_REQ;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = (s8 *)&scanSource;
	strWID.s32ValueSize = sizeof(char);

	return s32Error;
}

/**
 *  @brief                      host_int_get_start_scan_req
 *  @details            gets a start scan request
 *  @param[in,out] handle to the wifi driver,
 *  @param[in]	Scan Source one of the following values
 *                              DEFAULT_SCAN        0
 *                              USER_SCAN           BIT0
 *                              OBSS_PERIODIC_SCAN  BIT1
 *                              OBSS_ONETIME_SCAN   BIT2
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */

s32 host_int_get_start_scan_req(tstrWILC_WFIDrv *hWFIDrv, u8 *pu8ScanSource)
{
	s32 s32Error = 0;
	tstrWID strWID;
	/* tstrWILC_WFIDrv * pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv; */

	strWID.u16WIDid = (u16)WID_START_SCAN_REQ;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = (s8 *)pu8ScanSource;
	strWID.s32ValueSize = sizeof(char);

	return s32Error;
}

/**
 *  @brief                      host_int_set_join_req
 *  @details            sets a join request
 *  @param[in,out] handle to the wifi driver,
 *  @param[in]	Index of the bss descriptor
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_set_join_req(tstrWILC_WFIDrv *hWFIDrv, u8 *pu8bssid,
				  const u8 *pu8ssid, size_t ssidLen,
				  const u8 *pu8IEs, size_t IEsLen,
				  tWILCpfConnectResult pfConnectResult, void *pvUserArg,
				  u8 u8security, AUTHTYPE_T tenuAuth_type,
				  u8 u8channel,
				  void *pJoinParams)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	tenuScanConnTimer enuScanConnTimer;

	if (pstrWFIDrv == NULL || pfConnectResult == NULL) {
		s32Error = -EFAULT;
		PRINT_ER("Driver is null\n");
		return s32Error;
	}

	if (hWFIDrv == NULL) {
		PRINT_ER("Driver is null\n");
		return -EFAULT;
	}

	if (pJoinParams == NULL) {
		PRINT_ER("Unable to Join - JoinParams is NULL\n");
		return -EFAULT;
	}

	/* prepare the Connect Message */
	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_CONNECT;

	msg.body.con_info.u8security = u8security;
	msg.body.con_info.tenuAuth_type = tenuAuth_type;
	msg.body.con_info.u8channel = u8channel;
	msg.body.con_info.pfConnectResult = pfConnectResult;
	msg.body.con_info.pvUserArg = pvUserArg;
	msg.body.con_info.pJoinParams = pJoinParams;
	msg.drvHandler = hWFIDrv;

	if (pu8bssid != NULL) {
		msg.body.con_info.pu8bssid = kmalloc(6, GFP_KERNEL); /* will be deallocated by the receiving thread */
		memcpy(msg.body.con_info.pu8bssid,
			    pu8bssid, 6);
	}

	if (pu8ssid != NULL) {
		msg.body.con_info.ssidLen = ssidLen;
		msg.body.con_info.pu8ssid = kmalloc(ssidLen, GFP_KERNEL); /* will be deallocated by the receiving thread */
		memcpy(msg.body.con_info.pu8ssid,

			    pu8ssid, ssidLen);
	}

	if (pu8IEs != NULL) {
		msg.body.con_info.IEsLen = IEsLen;
		msg.body.con_info.pu8IEs = kmalloc(IEsLen, GFP_KERNEL); /* will be deallocated by the receiving thread */
		memcpy(msg.body.con_info.pu8IEs,
			    pu8IEs, IEsLen);
	}
	if (pstrWFIDrv->enuHostIFstate < HOST_IF_CONNECTING)
		pstrWFIDrv->enuHostIFstate = HOST_IF_CONNECTING;
	else
		PRINT_D(GENERIC_DBG, "Don't set state to 'connecting' as state is %d\n", pstrWFIDrv->enuHostIFstate);

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("Failed to send message queue: Set join request\n");
		return -EFAULT;
	}

	enuScanConnTimer = CONNECT_TIMER;
	pstrWFIDrv->hConnectTimer.data = (unsigned long)hWFIDrv;
	mod_timer(&pstrWFIDrv->hConnectTimer,
		  jiffies + msecs_to_jiffies(HOST_IF_CONNECT_TIMEOUT));

	return s32Error;
}

/**
 *  @brief              Flush a join request parameters to FW, but actual connection
 *  @details    The function is called in situation where WILC is connected to AP and
 *                      required to switch to hybrid FW for P2P connection
 *  @param[in] handle to the wifi driver,
 *  @return     Error code indicating success/failure
 *  @note
 *  @author	Amr Abdel-Moghny
 *  @date		19 DEC 2013
 *  @version	8.0
 */

s32 host_int_flush_join_req(tstrWILC_WFIDrv *hWFIDrv)
{
	s32 s32Error = 0;
	struct host_if_msg msg;

	if (!gu8FlushedJoinReq)	{
		s32Error = -EFAULT;
		return s32Error;
	}


	if (hWFIDrv  == NULL) {
		s32Error = -EFAULT;
		PRINT_ER("Driver is null\n");
		return s32Error;
	}

	msg.id = HOST_IF_MSG_FLUSH_CONNECT;
	msg.drvHandler = hWFIDrv;

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("Failed to send message queue: Flush join request\n");
		return -EFAULT;
	}

	return s32Error;
}

/**
 *  @brief                      host_int_disconnect
 *  @details            disconnects from the currently associated network
 *  @param[in,out] handle to the wifi driver,
 *  @param[in]	Reason Code of the Disconnection
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_disconnect(tstrWILC_WFIDrv *hWFIDrv, u16 u16ReasonCode)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("Driver is null\n");
		return -EFAULT;
	}

	/* prepare the Disconnect Message */
	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_DISCONNECT;
	msg.drvHandler = hWFIDrv;

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Failed to send message queue: disconnect\n");
	/* ////////////// */
	down(&(pstrWFIDrv->hSemTestDisconnectBlock));
	/* /////// */

	return s32Error;
}

/**
 *  @brief              host_int_disconnect_station
 *  @details     disconnects a sta
 *  @param[in,out] handle to the wifi driver,
 *  @param[in]	Association Id of the station to be disconnected
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_disconnect_station(tstrWILC_WFIDrv *hWFIDrv, u8 assoc_id)
{
	s32 s32Error = 0;
	tstrWID strWID;
	/* tstrWILC_WFIDrv * pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv; */

	strWID.u16WIDid = (u16)WID_DISCONNECT;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = (s8 *)&assoc_id;
	strWID.s32ValueSize = sizeof(char);

	return s32Error;
}

/**
 *  @brief                      host_int_get_assoc_req_info
 *  @details            gets a Association request info
 *  @param[in,out] handle to the wifi driver,
 *                              Message containg assoc. req info in the following format
 * ------------------------------------------------------------------------
 |                        Management Frame Format                    |
 ||-------------------------------------------------------------------|
 ||Frame Control|Duration|DA|SA|BSSID|Sequence Control|Frame Body|FCS |
 ||-------------|--------|--|--|-----|----------------|----------|----|
 | 2           |2       |6 |6 |6    |		2       |0 - 2312  | 4  |
 ||-------------------------------------------------------------------|
 |                                                                   |
 |             Association Request Frame - Frame Body                |
 ||-------------------------------------------------------------------|
 | Capability Information | Listen Interval | SSID | Supported Rates |
 ||------------------------|-----------------|------|-----------------|
 |			2            |		 2         | 2-34 |		3-10        |
 | ---------------------------------------------------------------------
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */

s32 host_int_get_assoc_req_info(tstrWILC_WFIDrv *hWFIDrv, u8 *pu8AssocReqInfo,
					u32 u32AssocReqInfoLen)
{
	s32 s32Error = 0;
	tstrWID strWID;
	/* tstrWILC_WFIDrv * pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv; */

	strWID.u16WIDid = (u16)WID_ASSOC_REQ_INFO;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = pu8AssocReqInfo;
	strWID.s32ValueSize = u32AssocReqInfoLen;


	return s32Error;
}

/**
 *  @brief              gets a Association Response info
 *  @details
 *  @param[in,out] handle to the wifi driver,
 *                              Message containg assoc. resp info
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_get_assoc_res_info(tstrWILC_WFIDrv *hWFIDrv, u8 *pu8AssocRespInfo,
					u32 u32MaxAssocRespInfoLen, u32 *pu32RcvdAssocRespInfoLen)
{
	s32 s32Error = 0;
	tstrWID strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("Driver is null\n");
		return -EFAULT;
	}

	strWID.u16WIDid = (u16)WID_ASSOC_RES_INFO;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = pu8AssocRespInfo;
	strWID.s32ValueSize = u32MaxAssocRespInfoLen;


	/* Sending Configuration packet */
	s32Error = send_config_pkt(GET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error) {
		*pu32RcvdAssocRespInfoLen = 0;
		PRINT_ER("Failed to send association response config packet\n");
		return -EINVAL;
	} else {
		*pu32RcvdAssocRespInfoLen = strWID.s32ValueSize;
	}

	return s32Error;
}

/**
 *  @brief              gets a Association Response info
 *  @details    Valid only in STA mode. This function gives the RSSI
 *                              values observed in all the channels at the time of scanning.
 *                              The length of the field is 1 greater that the total number of
 *                              channels supported. Byte 0 contains the number of channels while
 *                              each of Byte N contains	the observed RSSI value for the channel index N.
 *  @param[in,out] handle to the wifi driver,
 *                              array of scanned channels' RSSI
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_get_rx_power_level(tstrWILC_WFIDrv *hWFIDrv, u8 *pu8RxPowerLevel,
					u32 u32RxPowerLevelLen)
{
	s32 s32Error = 0;
	tstrWID strWID;
	/* tstrWILC_WFIDrv * pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv; */

	strWID.u16WIDid = (u16)WID_RX_POWER_LEVEL;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = pu8RxPowerLevel;
	strWID.s32ValueSize = u32RxPowerLevelLen;


	return s32Error;
}

/**
 *  @brief              sets a channel
 *  @details
 *  @param[in,out] handle to the wifi driver,
 *  @param[in]	Index of the channel to be set
 *|-------------------------------------------------------------------|
 |          CHANNEL1      CHANNEL2 ....		             CHANNEL14	|
 |  Input:         1             2					            14	|
 ||-------------------------------------------------------------------|
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
int host_int_set_mac_chnl_num(tstrWILC_WFIDrv *wfi_drv, u8 channel)
{
	int result = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)wfi_drv;
	struct host_if_msg msg;

	if (!pstrWFIDrv) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	/* prepare the set channel message */
	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_SET_CHANNEL;
	msg.body.channel_info.u8SetChan = channel;
	msg.drvHandler = wfi_drv;

	result = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (result) {
		PRINT_ER("wilc mq send fail\n");
		result = -EINVAL;
	}

	return result;
}


int host_int_wait_msg_queue_idle(void)
{
	int result = 0;

	struct host_if_msg msg;

	/* prepare the set driver handler message */

	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_Q_IDLE;
	result = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (result) {
		PRINT_ER("wilc mq send fail\n");
		result = -EINVAL;
	}

	/* wait untill MSG Q is empty */
	down(&hWaitResponse);

	return result;
}

s32 host_int_set_wfi_drv_handler(tstrWILC_WFIDrv *u32address)
{
	s32 s32Error = 0;

	struct host_if_msg msg;


	/* prepare the set driver handler message */

	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_SET_WFIDRV_HANDLER;
	msg.body.drv.u32Address = get_id_from_handler(u32address);
	msg.drvHandler = u32address;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("wilc mq send fail\n");
		s32Error = -EINVAL;
	}

	return s32Error;
}



s32 host_int_set_operation_mode(tstrWILC_WFIDrv *hWFIDrv, u32 u32mode)
{
	s32 s32Error = 0;

	struct host_if_msg msg;


	/* prepare the set driver handler message */

	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_SET_OPERATION_MODE;
	msg.body.mode.u32Mode = u32mode;
	msg.drvHandler = hWFIDrv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("wilc mq send fail\n");
		s32Error = -EINVAL;
	}

	return s32Error;
}

/**
 *  @brief              gets the current channel index
 *  @details
 *  @param[in,out] handle to the wifi driver,
 *                              current channel index
 *|-----------------------------------------------------------------------|
 |          CHANNEL1      CHANNEL2 ....                     CHANNEL14	|
 |  Input:         1             2                                 14	|
 ||-----------------------------------------------------------------------|
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_get_host_chnl_num(tstrWILC_WFIDrv *hWFIDrv, u8 *pu8ChNo)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	/* prepare the Get Channel Message */
	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_GET_CHNL;
	msg.drvHandler = hWFIDrv;

	/* send the message */
	s32Error =	wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc mq send fail\n");
	down(&(pstrWFIDrv->hSemGetCHNL));
	/* gu8Chnl = 11; */

	*pu8ChNo = gu8Chnl;

	return s32Error;


}


/**
 *  @brief                       host_int_test_set_int_wid
 *  @details             Test function for setting wids
 *  @param[in,out]   WILC_WFIDrvHandle hWFIDrv, u32 u32TestMemAddr
 *  @return              Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_test_set_int_wid(tstrWILC_WFIDrv *hWFIDrv, u32 u32TestMemAddr)
{
	s32 s32Error = 0;
	tstrWID	strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;


	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_MEMORY_ADDRESS;
	strWID.enuWIDtype = WID_INT;
	strWID.ps8WidVal = (char *)&u32TestMemAddr;
	strWID.s32ValueSize = sizeof(u32);

	/*Sending Cfg*/
	s32Error = send_config_pkt(SET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	if (s32Error) {
		PRINT_ER("Failed to set wid value\n");
		return -EINVAL;
	} else {
		PRINT_D(HOSTINF_DBG, "Successfully set wid value\n");

	}

	return s32Error;
}

/**
 *  @brief              host_int_get_inactive_time
 *  @details
 *  @param[in,out] handle to the wifi driver,
 *                              current sta macaddress, inactive_time
 *  @return
 *  @note
 *  @author
 *  @date
 *  @version		1.0
 */
s32 host_int_get_inactive_time(tstrWILC_WFIDrv *hWFIDrv, const u8 *mac, u32 *pu32InactiveTime)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));


	memcpy(msg.body.mac_info.mac,
		    mac, ETH_ALEN);

	msg.id = HOST_IF_MSG_GET_INACTIVETIME;
	msg.drvHandler = hWFIDrv;

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Failed to send get host channel param's message queue ");

	down(&(pstrWFIDrv->hSemInactiveTime));

	*pu32InactiveTime = gu32InactiveTime;

	return s32Error;
}

/**
 *  @brief              host_int_test_get_int_wid
 *  @details    Test function for getting wids
 *  @param[in,out] WILC_WFIDrvHandle hWFIDrv, u32* pu32TestMemAddr
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_test_get_int_wid(tstrWILC_WFIDrv *hWFIDrv, u32 *pu32TestMemAddr)
{

	s32 s32Error = 0;
	tstrWID	strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;


	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	strWID.u16WIDid = (u16)WID_MEMORY_ADDRESS;
	strWID.enuWIDtype = WID_INT;
	strWID.ps8WidVal = (s8 *)pu32TestMemAddr;
	strWID.s32ValueSize = sizeof(u32);

	s32Error = send_config_pkt(GET_CFG, &strWID, 1, true,
				   get_id_from_handler(pstrWFIDrv));
	/*get the value by searching the local copy*/
	if (s32Error) {
		PRINT_ER("Failed to get wid value\n");
		return -EINVAL;
	} else {
		PRINT_D(HOSTINF_DBG, "Successfully got wid value\n");

	}

	return s32Error;
}


/**
 *  @brief              host_int_get_rssi
 *  @details    gets the currently maintained RSSI value for the station.
 *                              The received signal strength value in dB.
 *                              The range of valid values is -128 to 0.
 *  @param[in,out] handle to the wifi driver,
 *                              rssi value in dB
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_get_rssi(tstrWILC_WFIDrv *hWFIDrv, s8 *ps8Rssi)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;


	/* prepare the Get RSSI Message */
	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_GET_RSSI;
	msg.drvHandler = hWFIDrv;

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("Failed to send get host channel param's message queue ");
		return -EFAULT;
	}

	down(&(pstrWFIDrv->hSemGetRSSI));


	if (ps8Rssi == NULL) {
		PRINT_ER("RSS pointer value is null");
		return -EFAULT;
	}


	*ps8Rssi = gs8Rssi;


	return s32Error;
}

s32 host_int_get_link_speed(tstrWILC_WFIDrv *hWFIDrv, s8 *ps8lnkspd)
{
	struct host_if_msg msg;
	s32 s32Error = 0;

	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;



	/* prepare the Get LINKSPEED Message */
	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_GET_LINKSPEED;
	msg.drvHandler = hWFIDrv;

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("Failed to send GET_LINKSPEED to message queue ");
		return -EFAULT;
	}

	down(&(pstrWFIDrv->hSemGetLINKSPEED));


	if (ps8lnkspd == NULL) {
		PRINT_ER("LINKSPEED pointer value is null");
		return -EFAULT;
	}


	*ps8lnkspd = gs8lnkspd;


	return s32Error;
}

s32 host_int_get_statistics(tstrWILC_WFIDrv *hWFIDrv, tstrStatistics *pstrStatistics)
{
	s32 s32Error = 0;
	struct host_if_msg msg;


	/* prepare the Get RSSI Message */
	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_GET_STATISTICS;
	msg.body.pUserData = (char *)pstrStatistics;
	msg.drvHandler = hWFIDrv;
	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("Failed to send get host channel param's message queue ");
		return -EFAULT;
	}

	down(&hWaitResponse);
	return s32Error;
}


/**
 *  @brief              host_int_scan
 *  @details    scans a set of channels
 *  @param[in,out] handle to the wifi driver,
 *  @param[in]	Scan source
 *                              Scan Type	PASSIVE_SCAN = 0,
 *                                                      ACTIVE_SCAN  = 1
 *                              Channels Array
 *                              Channels Array length
 *                              Scan Callback function
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 host_int_scan(tstrWILC_WFIDrv *hWFIDrv, u8 u8ScanSource,
			  u8 u8ScanType, u8 *pu8ChnlFreqList,
			  u8 u8ChnlListLen, const u8 *pu8IEs,
			  size_t IEsLen, tWILCpfScanResult ScanResult,
			  void *pvUserArg, tstrHiddenNetwork  *pstrHiddenNetwork)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	tenuScanConnTimer enuScanConnTimer;

	if (pstrWFIDrv == NULL || ScanResult == NULL) {
		PRINT_ER("pstrWFIDrv or ScanResult = NULL\n");
		return -EFAULT;
	}

	/* prepare the Scan Message */
	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_SCAN;

	if (pstrHiddenNetwork != NULL) {
		msg.body.scan_info.strHiddenNetwork.pstrHiddenNetworkInfo = pstrHiddenNetwork->pstrHiddenNetworkInfo;
		msg.body.scan_info.strHiddenNetwork.u8ssidnum = pstrHiddenNetwork->u8ssidnum;

	} else
		PRINT_D(HOSTINF_DBG, "pstrHiddenNetwork IS EQUAL TO NULL\n");

	msg.drvHandler = hWFIDrv;
	msg.body.scan_info.u8ScanSource = u8ScanSource;
	msg.body.scan_info.u8ScanType = u8ScanType;
	msg.body.scan_info.pfScanResult = ScanResult;
	msg.body.scan_info.pvUserArg = pvUserArg;

	msg.body.scan_info.u8ChnlListLen = u8ChnlListLen;
	msg.body.scan_info.pu8ChnlFreqList = kmalloc(u8ChnlListLen, GFP_KERNEL);        /* will be deallocated by the receiving thread */
	memcpy(msg.body.scan_info.pu8ChnlFreqList,
		    pu8ChnlFreqList, u8ChnlListLen);

	msg.body.scan_info.IEsLen = IEsLen;
	msg.body.scan_info.pu8IEs = kmalloc(IEsLen, GFP_KERNEL);        /* will be deallocated by the receiving thread */
	memcpy(msg.body.scan_info.pu8IEs,
		    pu8IEs, IEsLen);

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error) {
		PRINT_ER("Error in sending message queue\n");
		return -EINVAL;
	}

	enuScanConnTimer = SCAN_TIMER;
	PRINT_D(HOSTINF_DBG, ">> Starting the SCAN timer\n");
	pstrWFIDrv->hScanTimer.data = (unsigned long)hWFIDrv;
	mod_timer(&pstrWFIDrv->hScanTimer,
		  jiffies + msecs_to_jiffies(HOST_IF_SCAN_TIMEOUT));

	return s32Error;

}
/**
 *  @brief                      hif_set_cfg
 *  @details            sets configuration wids values
 *  @param[in,out] handle to the wifi driver,
 *  @param[in]	WID, WID value
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 hif_set_cfg(tstrWILC_WFIDrv *hWFIDrv, tstrCfgParamVal *pstrCfgParamVal)
{

	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;

	struct host_if_msg msg;


	if (pstrWFIDrv == NULL) {
		PRINT_ER("pstrWFIDrv NULL\n");
		return -EFAULT;
	}
	/* prepare the WiphyParams Message */
	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_CFG_PARAMS;
	msg.body.cfg_info.pstrCfgParamVal = *pstrCfgParamVal;
	msg.drvHandler = hWFIDrv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));

	return s32Error;

}


/**
 *  @brief              hif_get_cfg
 *  @details    gets configuration wids values
 *  @param[in,out] handle to the wifi driver,
 *                              WID value
 *  @param[in]	WID,
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *
 *  @date		8 March 2012
 *  @version		1.0
 */
s32 hif_get_cfg(tstrWILC_WFIDrv *hWFIDrv, u16 u16WID, u16 *pu16WID_Value)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;

	down(&(pstrWFIDrv->gtOsCfgValuesSem));

	if (pstrWFIDrv == NULL) {
		PRINT_ER("pstrWFIDrv NULL\n");
		return -EFAULT;
	}
	PRINT_D(HOSTINF_DBG, "Getting configuration parameters\n");
	switch (u16WID)	{

	case WID_BSS_TYPE:
		*pu16WID_Value = (u16)pstrWFIDrv->strCfgValues.bss_type;
		break;

	case WID_AUTH_TYPE:
		*pu16WID_Value = (u16)pstrWFIDrv->strCfgValues.auth_type;
		break;

	case WID_AUTH_TIMEOUT:
		*pu16WID_Value = pstrWFIDrv->strCfgValues.auth_timeout;
		break;

	case WID_POWER_MANAGEMENT:
		*pu16WID_Value = (u16)pstrWFIDrv->strCfgValues.power_mgmt_mode;
		break;

	case WID_SHORT_RETRY_LIMIT:
		*pu16WID_Value =       pstrWFIDrv->strCfgValues.short_retry_limit;
		break;

	case WID_LONG_RETRY_LIMIT:
		*pu16WID_Value = pstrWFIDrv->strCfgValues.long_retry_limit;
		break;

	case WID_FRAG_THRESHOLD:
		*pu16WID_Value = pstrWFIDrv->strCfgValues.frag_threshold;
		break;

	case WID_RTS_THRESHOLD:
		*pu16WID_Value = pstrWFIDrv->strCfgValues.rts_threshold;
		break;

	case WID_PREAMBLE:
		*pu16WID_Value = (u16)pstrWFIDrv->strCfgValues.preamble_type;
		break;

	case WID_SHORT_SLOT_ALLOWED:
		*pu16WID_Value = (u16) pstrWFIDrv->strCfgValues.short_slot_allowed;
		break;

	case WID_11N_TXOP_PROT_DISABLE:
		*pu16WID_Value = (u16)pstrWFIDrv->strCfgValues.txop_prot_disabled;
		break;

	case WID_BEACON_INTERVAL:
		*pu16WID_Value = pstrWFIDrv->strCfgValues.beacon_interval;
		break;

	case WID_DTIM_PERIOD:
		*pu16WID_Value = (u16)pstrWFIDrv->strCfgValues.dtim_period;
		break;

	case WID_SITE_SURVEY:
		*pu16WID_Value = (u16)pstrWFIDrv->strCfgValues.site_survey_enabled;
		break;

	case WID_SITE_SURVEY_SCAN_TIME:
		*pu16WID_Value = pstrWFIDrv->strCfgValues.site_survey_scan_time;
		break;

	case WID_ACTIVE_SCAN_TIME:
		*pu16WID_Value = pstrWFIDrv->strCfgValues.active_scan_time;
		break;

	case WID_PASSIVE_SCAN_TIME:
		*pu16WID_Value = pstrWFIDrv->strCfgValues.passive_scan_time;
		break;

	case WID_CURRENT_TX_RATE:
		*pu16WID_Value = pstrWFIDrv->strCfgValues.curr_tx_rate;
		break;

	default:
		break;
	}

	up(&(pstrWFIDrv->gtOsCfgValuesSem));

	return s32Error;

}

/*****************************************************************************/
/*							Notification Functions							 */
/*****************************************************************************/
/**
 *  @brief              notifies host with join and leave requests
 *  @details    This function prepares an Information frame having the
 *                              information about a joining/leaving station.
 *  @param[in,out] handle to the wifi driver,
 *  @param[in]	6 byte Sta Adress
 *                              Join or leave flag:
 *                              Join = 1,
 *                              Leave =0
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
void host_int_send_join_leave_info_to_host
	(u16 assocId, u8 *stationAddr, bool joining)
{
}
/**
 *  @brief              notifies host with stations found in scan
 *  @details    sends the beacon/probe response from scan
 *  @param[in,out] handle to the wifi driver,
 *  @param[in]	Sta Address,
 *                              Frame length,
 *                              Rssi of the Station found
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */

static void GetPeriodicRSSI(unsigned long arg)
{
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)arg;

	if (pstrWFIDrv == NULL)	{
		PRINT_ER("Driver handler is NULL\n");
		return;
	}

	if (pstrWFIDrv->enuHostIFstate == HOST_IF_CONNECTED) {
		s32 s32Error = 0;
		struct host_if_msg msg;

		/* prepare the Get RSSI Message */
		memset(&msg, 0, sizeof(struct host_if_msg));

		msg.id = HOST_IF_MSG_GET_RSSI;
		msg.drvHandler = pstrWFIDrv;

		/* send the message */
		s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
		if (s32Error) {
			PRINT_ER("Failed to send get host channel param's message queue ");
			return;
		}
	}
	g_hPeriodicRSSI.data = (unsigned long)pstrWFIDrv;
	mod_timer(&g_hPeriodicRSSI, jiffies + msecs_to_jiffies(5000));
}


void host_int_send_network_info_to_host
	(u8 *macStartAddress, u16 u16RxFrameLen, s8 s8Rssi)
{
}
/**
 *  @brief              host_int_init
 *  @details    host interface initialization function
 *  @param[in,out] handle to the wifi driver,
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
static u32 clients_count;

s32 host_int_init(tstrWILC_WFIDrv **phWFIDrv)
{
	s32 result = 0;
	tstrWILC_WFIDrv *pstrWFIDrv;
	int err;

	PRINT_D(HOSTINF_DBG, "Initializing host interface for client %d\n", clients_count + 1);

	gbScanWhileConnected = false;

	sema_init(&hWaitResponse, 0);

	/*Allocate host interface private structure*/
	pstrWFIDrv  = kzalloc(sizeof(tstrWILC_WFIDrv), GFP_KERNEL);
	if (!pstrWFIDrv) {
		result = -ENOMEM;
		goto _fail_timer_2;
	}
	*phWFIDrv = pstrWFIDrv;
	err = add_handler_in_list(pstrWFIDrv);
	if (err) {
		result = -EFAULT;
		goto _fail_timer_2;
	}

	g_obtainingIP = false;

	PRINT_D(HOSTINF_DBG, "Global handle pointer value=%p\n", pstrWFIDrv);
	if (clients_count == 0)	{
		sema_init(&hSemHostIFthrdEnd, 0);
		sema_init(&hSemDeinitDrvHandle, 0);
		sema_init(&hSemHostIntDeinit, 1);
	}

	sema_init(&pstrWFIDrv->hSemTestKeyBlock, 0);
	sema_init(&pstrWFIDrv->hSemTestDisconnectBlock, 0);
	sema_init(&pstrWFIDrv->hSemGetRSSI, 0);
	sema_init(&pstrWFIDrv->hSemGetLINKSPEED, 0);
	sema_init(&pstrWFIDrv->hSemGetCHNL, 0);
	sema_init(&pstrWFIDrv->hSemInactiveTime, 0);

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
			    (unsigned long)pstrWFIDrv);
		mod_timer(&g_hPeriodicRSSI, jiffies + msecs_to_jiffies(5000));
	}

	setup_timer(&pstrWFIDrv->hScanTimer, TimerCB_Scan, 0);

	setup_timer(&pstrWFIDrv->hConnectTimer, TimerCB_Connect, 0);

	/*Remain on channel timer*/
	setup_timer(&pstrWFIDrv->hRemainOnChannel, ListenTimerCB, 0);

	sema_init(&(pstrWFIDrv->gtOsCfgValuesSem), 1);
	down(&pstrWFIDrv->gtOsCfgValuesSem);

	pstrWFIDrv->enuHostIFstate = HOST_IF_IDLE;

	/*Initialize CFG WIDS Defualt Values*/

	pstrWFIDrv->strCfgValues.site_survey_enabled = SITE_SURVEY_OFF;
	pstrWFIDrv->strCfgValues.scan_source = DEFAULT_SCAN;
	pstrWFIDrv->strCfgValues.active_scan_time = ACTIVE_SCAN_TIME;
	pstrWFIDrv->strCfgValues.passive_scan_time = PASSIVE_SCAN_TIME;
	pstrWFIDrv->strCfgValues.curr_tx_rate = AUTORATE;

	pstrWFIDrv->u64P2p_MgmtTimeout = 0;

	PRINT_INFO(HOSTINF_DBG, "Initialization values, Site survey value: %d\n Scan source: %d\n Active scan time: %d\n Passive scan time: %d\nCurrent tx Rate = %d\n",

		   pstrWFIDrv->strCfgValues.site_survey_enabled, pstrWFIDrv->strCfgValues.scan_source,
		   pstrWFIDrv->strCfgValues.active_scan_time, pstrWFIDrv->strCfgValues.passive_scan_time,
		   pstrWFIDrv->strCfgValues.curr_tx_rate);

	up(&pstrWFIDrv->gtOsCfgValuesSem);

	clients_count++; /* increase number of created entities */

	return result;

_fail_timer_2:
	up(&pstrWFIDrv->gtOsCfgValuesSem);
	del_timer_sync(&pstrWFIDrv->hConnectTimer);
	del_timer_sync(&pstrWFIDrv->hScanTimer);
	kthread_stop(HostIFthreadHandler);
_fail_mq_:
	wilc_mq_destroy(&gMsgQHostIF);
_fail_:
	return result;
}
/**
 *  @brief              host_int_deinit
 *  @details    host interface initialization function
 *  @param[in,out] handle to the wifi driver,
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */

s32 host_int_deinit(tstrWILC_WFIDrv *hWFIDrv)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	int ret;

	/*obtain driver handle*/
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;

	if (pstrWFIDrv == NULL)	{
		PRINT_ER("pstrWFIDrv = NULL\n");
		return 0;
	}

	down(&hSemHostIntDeinit);

	terminated_handle = pstrWFIDrv;
	PRINT_D(HOSTINF_DBG, "De-initializing host interface for client %d\n", clients_count);

	/*Destroy all timers before acquiring hSemDeinitDrvHandle*/
	/*to guarantee handling all messages befor proceeding*/
	if (del_timer_sync(&pstrWFIDrv->hScanTimer)) {
		PRINT_D(HOSTINF_DBG, ">> Scan timer is active\n");
		/* msleep(HOST_IF_SCAN_TIMEOUT+1000); */
	}

	if (del_timer_sync(&pstrWFIDrv->hConnectTimer)) {
		PRINT_D(HOSTINF_DBG, ">> Connect timer is active\n");
		/* msleep(HOST_IF_CONNECT_TIMEOUT+1000); */
	}


	if (del_timer_sync(&g_hPeriodicRSSI)) {
		PRINT_D(HOSTINF_DBG, ">> Connect timer is active\n");
		/* msleep(HOST_IF_CONNECT_TIMEOUT+1000); */
	}

	/*Destroy Remain-onchannel Timer*/
	del_timer_sync(&pstrWFIDrv->hRemainOnChannel);

	host_int_set_wfi_drv_handler(NULL);
	down(&hSemDeinitDrvHandle);


	/*Calling the CFG80211 scan done function with the abort flag set to true*/
	if (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult) {
		pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult(SCAN_EVENT_ABORTED, NULL,
								pstrWFIDrv->strWILC_UsrScanReq.u32UserScanPvoid, NULL);

		pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult = NULL;
	}

	pstrWFIDrv->enuHostIFstate = HOST_IF_IDLE;

	gbScanWhileConnected = false;

	memset(&msg, 0, sizeof(struct host_if_msg));

	if (clients_count == 1)	{
		if (del_timer_sync(&g_hPeriodicRSSI)) {
			PRINT_D(HOSTINF_DBG, ">> Connect timer is active\n");
			/* msleep(HOST_IF_CONNECT_TIMEOUT+1000); */
		}
		msg.id = HOST_IF_MSG_EXIT;
		msg.drvHandler = hWFIDrv;


		s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
		if (s32Error != 0)
			PRINT_ER("Error in sending deinit's message queue message function: Error(%d)\n", s32Error);

		down(&hSemHostIFthrdEnd);

		wilc_mq_destroy(&gMsgQHostIF);
	}

	down(&(pstrWFIDrv->gtOsCfgValuesSem));

	/*Setting the gloabl driver handler with NULL*/
	/* gWFiDrvHandle = NULL; */
	ret = remove_handler_in_list(pstrWFIDrv);
	if (ret)
		s32Error = -ENOENT;

	if (pstrWFIDrv != NULL) {
		kfree(pstrWFIDrv);
		/* pstrWFIDrv=NULL; */

	}

	clients_count--; /* Decrease number of created entities */
	terminated_handle = NULL;
	up(&hSemHostIntDeinit);
	return s32Error;
}


/**
 *  @brief              NetworkInfoReceived
 *  @details    function to to be called when network info packet is received
 *  @param[in]	pu8Buffer the received packet
 *  @param[in]   u32Length  length of the received packet
 *  @return             none
 *  @note
 *  @author
 *  @date		1 Mar 2012
 *  @version		1.0
 */
void NetworkInfoReceived(u8 *pu8Buffer, u32 u32Length)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	int id;
	tstrWILC_WFIDrv *pstrWFIDrv = NULL;

	id = ((pu8Buffer[u32Length - 4]) | (pu8Buffer[u32Length - 3] << 8) | (pu8Buffer[u32Length - 2] << 16) | (pu8Buffer[u32Length - 1] << 24));
	pstrWFIDrv = get_handler_from_id(id);




	if (pstrWFIDrv == NULL || pstrWFIDrv == terminated_handle)	{
		PRINT_ER("NetworkInfo received but driver not init[%p]\n", pstrWFIDrv);
		return;
	}

	/* prepare the Asynchronous Network Info message */
	memset(&msg, 0, sizeof(struct host_if_msg));

	msg.id = HOST_IF_MSG_RCVD_NTWRK_INFO;
	msg.drvHandler = pstrWFIDrv;

	msg.body.net_info.u32Length = u32Length;
	msg.body.net_info.pu8Buffer = kmalloc(u32Length, GFP_KERNEL); /* will be deallocated by the receiving thread */
	memcpy(msg.body.net_info.pu8Buffer,
		    pu8Buffer, u32Length);

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Error in sending network info message queue message parameters: Error(%d)\n", s32Error);
}

/**
 *  @brief              GnrlAsyncInfoReceived
 *  @details    function to be called when general Asynchronous info packet is received
 *  @param[in]	pu8Buffer the received packet
 *  @param[in]   u32Length  length of the received packet
 *  @return             none
 *  @note
 *  @author
 *  @date		15 Mar 2012
 *  @version		1.0
 */
void GnrlAsyncInfoReceived(u8 *pu8Buffer, u32 u32Length)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	int id;
	tstrWILC_WFIDrv *pstrWFIDrv = NULL;

	down(&hSemHostIntDeinit);

	id = ((pu8Buffer[u32Length - 4]) | (pu8Buffer[u32Length - 3] << 8) | (pu8Buffer[u32Length - 2] << 16) | (pu8Buffer[u32Length - 1] << 24));
	pstrWFIDrv = get_handler_from_id(id);
	PRINT_D(HOSTINF_DBG, "General asynchronous info packet received\n");


	if (pstrWFIDrv == NULL || pstrWFIDrv == terminated_handle) {
		PRINT_D(HOSTINF_DBG, "Wifi driver handler is equal to NULL\n");
		up(&hSemHostIntDeinit);
		return;
	}

	if (pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult == NULL) {
		/* received mac status is not needed when there is no current Connect Request */
		PRINT_ER("Received mac status is not needed when there is no current Connect Reques\n");
		up(&hSemHostIntDeinit);
		return;
	}

	/* prepare the General Asynchronous Info message */
	memset(&msg, 0, sizeof(struct host_if_msg));


	msg.id = HOST_IF_MSG_RCVD_GNRL_ASYNC_INFO;
	msg.drvHandler = pstrWFIDrv;


	msg.body.async_info.u32Length = u32Length;
	msg.body.async_info.pu8Buffer = kmalloc(u32Length, GFP_KERNEL); /* will be deallocated by the receiving thread */
	memcpy(msg.body.async_info.pu8Buffer,
		    pu8Buffer, u32Length);

	/* send the message */
	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("Error in sending message queue asynchronous message info: Error(%d)\n", s32Error);

	up(&hSemHostIntDeinit);
}

/**
 *  @brief host_int_ScanCompleteReceived
 *  @details        Setting scan complete received notifcation in message queue
 *  @param[in]     u8* pu8Buffer, u32 u32Length
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
void host_int_ScanCompleteReceived(u8 *pu8Buffer, u32 u32Length)
{
	s32 s32Error = 0;
	struct host_if_msg msg;
	int id;
	tstrWILC_WFIDrv *pstrWFIDrv = NULL;

	id = ((pu8Buffer[u32Length - 4]) | (pu8Buffer[u32Length - 3] << 8) | (pu8Buffer[u32Length - 2] << 16) | (pu8Buffer[u32Length - 1] << 24));
	pstrWFIDrv = get_handler_from_id(id);


	PRINT_D(GENERIC_DBG, "Scan notification received %p\n", pstrWFIDrv);

	if (pstrWFIDrv == NULL || pstrWFIDrv == terminated_handle)
		return;

	/*if there is an ongoing scan request*/
	if (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult) {
		/* prepare theScan Done message */
		memset(&msg, 0, sizeof(struct host_if_msg));

		msg.id = HOST_IF_MSG_RCVD_SCAN_COMPLETE;
		msg.drvHandler = pstrWFIDrv;


		/* will be deallocated by the receiving thread */
		/*no need to send message body*/

		/*msg.body.strScanComplete.u32Length = u32Length;
		 * msg.body.strScanComplete.pu8Buffer  = (u8*)WILC_MALLOC(u32Length);
		 * memcpy(msg.body.strScanComplete.pu8Buffer,
		 *                        pu8Buffer, u32Length); */

		/* send the message */
		s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
		if (s32Error)
			PRINT_ER("Error in sending message queue scan complete parameters: Error(%d)\n", s32Error);
	}


	return;

}

/**
 *  @brief              host_int_remain_on_channel
 *  @details
 *  @param[in]          Handle to wifi driver
 *                              Duration to remain on channel
 *                              Channel to remain on
 *                              Pointer to fn to be called on receive frames in listen state
 *                              Pointer to remain-on-channel expired fn
 *                              Priv
 *  @return             Error code.
 *  @author
 *  @date
 *  @version		1.0
 */
s32 host_int_remain_on_channel(tstrWILC_WFIDrv *hWFIDrv, u32 u32SessionID, u32 u32duration, u16 chan, tWILCpfRemainOnChanExpired RemainOnChanExpired, tWILCpfRemainOnChanReady RemainOnChanReady, void *pvUserArg)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	/* prepare the remainonchan Message */
	memset(&msg, 0, sizeof(struct host_if_msg));

	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_REMAIN_ON_CHAN;
	msg.body.remain_on_ch.u16Channel = chan;
	msg.body.remain_on_ch.pRemainOnChanExpired = RemainOnChanExpired;
	msg.body.remain_on_ch.pRemainOnChanReady = RemainOnChanReady;
	msg.body.remain_on_ch.pVoid = pvUserArg;
	msg.body.remain_on_ch.u32duration = u32duration;
	msg.body.remain_on_ch.u32ListenSessionID = u32SessionID;
	msg.drvHandler = hWFIDrv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc mq send fail\n");

	return s32Error;
}

/**
 *  @brief              host_int_ListenStateExpired
 *  @details
 *  @param[in]          Handle to wifi driver
 *                              Duration to remain on channel
 *                              Channel to remain on
 *                              Pointer to fn to be called on receive frames in listen state
 *                              Pointer to remain-on-channel expired fn
 *                              Priv
 *  @return             Error code.
 *  @author
 *  @date
 *  @version		1.0
 */
s32 host_int_ListenStateExpired(tstrWILC_WFIDrv *hWFIDrv, u32 u32SessionID)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	/*Stopping remain-on-channel timer*/
	del_timer(&pstrWFIDrv->hRemainOnChannel);

	/* prepare the timer fire Message */
	memset(&msg, 0, sizeof(struct host_if_msg));
	msg.id = HOST_IF_MSG_LISTEN_TIMER_FIRED;
	msg.drvHandler = hWFIDrv;
	msg.body.remain_on_ch.u32ListenSessionID = u32SessionID;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc mq send fail\n");

	return s32Error;
}

/**
 *  @brief              host_int_frame_register
 *  @details
 *  @param[in]          Handle to wifi driver
 *  @return             Error code.
 *  @author
 *  @date
 *  @version		1.0*/
s32 host_int_frame_register(tstrWILC_WFIDrv *hWFIDrv, u16 u16FrameType, bool bReg)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_REGISTER_FRAME;
	switch (u16FrameType) {
	case ACTION:
		PRINT_D(HOSTINF_DBG, "ACTION\n");
		msg.body.strHostIfRegisterFrame.u8Regid = ACTION_FRM_IDX;
		break;

	case PROBE_REQ:
		PRINT_D(HOSTINF_DBG, "PROBE REQ\n");
		msg.body.strHostIfRegisterFrame.u8Regid = PROBE_REQ_IDX;
		break;

	default:
		PRINT_D(HOSTINF_DBG, "Not valid frame type\n");
		break;
	}
	msg.body.strHostIfRegisterFrame.u16FrameType = u16FrameType;
	msg.body.strHostIfRegisterFrame.bReg = bReg;
	msg.drvHandler = hWFIDrv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc mq send fail\n");

	return s32Error;


}

/**
 *  @brief host_int_add_beacon
 *  @details       Setting add beacon params in message queue
 *  @param[in]    WILC_WFIDrvHandle hWFIDrv, u32 u32Interval,
 *                         u32 u32DTIMPeriod,u32 u32HeadLen, u8* pu8Head,
 *                         u32 u32TailLen, u8* pu8Tail
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
s32 host_int_add_beacon(tstrWILC_WFIDrv *hWFIDrv, u32 u32Interval,
				u32 u32DTIMPeriod,
				u32 u32HeadLen, u8 *pu8Head,
				u32 u32TailLen, u8 *pu8Tail)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	struct beacon_attr *pstrSetBeaconParam = &msg.body.beacon_info;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	PRINT_D(HOSTINF_DBG, "Setting adding beacon message queue params\n");


	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_ADD_BEACON;
	msg.drvHandler = hWFIDrv;
	pstrSetBeaconParam->u32Interval = u32Interval;
	pstrSetBeaconParam->u32DTIMPeriod = u32DTIMPeriod;
	pstrSetBeaconParam->u32HeadLen = u32HeadLen;
	pstrSetBeaconParam->pu8Head = kmalloc(u32HeadLen, GFP_KERNEL);
	if (pstrSetBeaconParam->pu8Head == NULL) {
		s32Error = -ENOMEM;
		goto ERRORHANDLER;
	}
	memcpy(pstrSetBeaconParam->pu8Head, pu8Head, u32HeadLen);
	pstrSetBeaconParam->u32TailLen = u32TailLen;

	if (u32TailLen > 0) {
		pstrSetBeaconParam->pu8Tail = kmalloc(u32TailLen, GFP_KERNEL);
		if (pstrSetBeaconParam->pu8Tail == NULL) {
			s32Error = -ENOMEM;
			goto ERRORHANDLER;
		}
		memcpy(pstrSetBeaconParam->pu8Tail, pu8Tail, u32TailLen);
	} else {
		pstrSetBeaconParam->pu8Tail = NULL;
	}

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc mq send fail\n");

ERRORHANDLER:
	if (s32Error) {
		if (pstrSetBeaconParam->pu8Head != NULL)
			kfree(pstrSetBeaconParam->pu8Head);

		if (pstrSetBeaconParam->pu8Tail != NULL)
			kfree(pstrSetBeaconParam->pu8Tail);
	}

	return s32Error;

}


/**
 *  @brief host_int_del_beacon
 *  @details       Setting add beacon params in message queue
 *  @param[in]    WILC_WFIDrvHandle hWFIDrv
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
s32 host_int_del_beacon(tstrWILC_WFIDrv *hWFIDrv)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_DEL_BEACON;
	msg.drvHandler = hWFIDrv;
	PRINT_D(HOSTINF_DBG, "Setting deleting beacon message queue params\n");

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");

	return s32Error;
}


/**
 *  @brief host_int_add_station
 *  @details       Setting add station params in message queue
 *  @param[in]    WILC_WFIDrvHandle hWFIDrv, struct add_sta_param *pstrStaParams
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
s32 host_int_add_station(tstrWILC_WFIDrv *hWFIDrv,
			 struct add_sta_param *pstrStaParams)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	struct add_sta_param *pstrAddStationMsg = &msg.body.add_sta_info;


	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	PRINT_D(HOSTINF_DBG, "Setting adding station message queue params\n");


	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_ADD_STATION;
	msg.drvHandler = hWFIDrv;

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

/**
 *  @brief host_int_del_station
 *  @details       Setting delete station params in message queue
 *  @param[in]    WILC_WFIDrvHandle hWFIDrv, u8* pu8MacAddr
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
s32 host_int_del_station(tstrWILC_WFIDrv *hWFIDrv, const u8 *pu8MacAddr)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	struct del_sta *pstrDelStationMsg = &msg.body.del_sta_info;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	PRINT_D(HOSTINF_DBG, "Setting deleting station message queue params\n");



	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_DEL_STATION;
	msg.drvHandler = hWFIDrv;

	if (pu8MacAddr == NULL)
		memset(pstrDelStationMsg->au8MacAddr, 255, ETH_ALEN);
	else
		memcpy(pstrDelStationMsg->au8MacAddr, pu8MacAddr, ETH_ALEN);

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");
	return s32Error;
}
/**
 *  @brief      host_int_del_allstation
 *  @details    Setting del station params in message queue
 *  @param[in]  WILC_WFIDrvHandle hWFIDrv, u8 pu8MacAddr[][ETH_ALEN]s
 *  @return        Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
s32 host_int_del_allstation(tstrWILC_WFIDrv *hWFIDrv, u8 pu8MacAddr[][ETH_ALEN])
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	struct del_all_sta *pstrDelAllStationMsg = &msg.body.strHostIFDelAllSta;
	u8 au8Zero_Buff[ETH_ALEN] = {0};
	u32 i;
	u8 u8AssocNumb = 0;


	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	PRINT_D(HOSTINF_DBG, "Setting deauthenticating station message queue params\n");

	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_DEL_ALL_STA;
	msg.drvHandler = hWFIDrv;

	/* Handling situation of deauthenticing all associated stations*/
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

/**
 *  @brief host_int_edit_station
 *  @details       Setting edit station params in message queue
 *  @param[in]    WILC_WFIDrvHandle hWFIDrv, struct add_sta_param *pstrStaParams
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
s32 host_int_edit_station(tstrWILC_WFIDrv *hWFIDrv,
			  struct add_sta_param *pstrStaParams)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	struct add_sta_param *pstrAddStationMsg = &msg.body.add_sta_info;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	PRINT_D(HOSTINF_DBG, "Setting editing station message queue params\n");

	memset(&msg, 0, sizeof(struct host_if_msg));


	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_EDIT_STATION;
	msg.drvHandler = hWFIDrv;

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

s32 host_int_set_power_mgmt(tstrWILC_WFIDrv *hWFIDrv, bool bIsEnabled, u32 u32Timeout)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	struct power_mgmt_param *pstrPowerMgmtParam = &msg.body.pwr_mgmt_info;

	PRINT_INFO(HOSTINF_DBG, "\n\n>> Setting PS to %d <<\n\n", bIsEnabled);

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	PRINT_D(HOSTINF_DBG, "Setting Power management message queue params\n");

	memset(&msg, 0, sizeof(struct host_if_msg));


	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_POWER_MGMT;
	msg.drvHandler = hWFIDrv;

	pstrPowerMgmtParam->bIsEnabled = bIsEnabled;
	pstrPowerMgmtParam->u32Timeout = u32Timeout;


	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");
	return s32Error;
}

s32 host_int_setup_multicast_filter(tstrWILC_WFIDrv *hWFIDrv, bool bIsEnabled, u32 u32count)
{
	s32 s32Error = 0;

	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	struct set_multicast *pstrMulticastFilterParam = &msg.body.multicast_info;


	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	PRINT_D(HOSTINF_DBG, "Setting Multicast Filter params\n");

	memset(&msg, 0, sizeof(struct host_if_msg));


	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_SET_MULTICAST_FILTER;
	msg.drvHandler = hWFIDrv;

	pstrMulticastFilterParam->bIsEnabled = bIsEnabled;
	pstrMulticastFilterParam->u32count = u32count;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");
	return s32Error;
}

/**
 *  @brief              host_int_ParseJoinBssParam
 *  @details            Parse Needed Join Parameters and save it in a new JoinBssParam entry
 *  @param[in]          tstrNetworkInfo* ptstrNetworkInfo
 *  @return
 *  @author		zsalah
 *  @date
 *  @version		1.0**/
static void *host_int_ParseJoinBssParam(tstrNetworkInfo *ptstrNetworkInfo)
{
	tstrJoinBssParam *pNewJoinBssParam = NULL;
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

	pNewJoinBssParam = kmalloc(sizeof(tstrJoinBssParam), GFP_KERNEL);
	if (pNewJoinBssParam != NULL) {
		memset(pNewJoinBssParam, 0, sizeof(tstrJoinBssParam));
		pNewJoinBssParam->dtim_period = ptstrNetworkInfo->u8DtimPeriod;
		pNewJoinBssParam->beacon_period = ptstrNetworkInfo->u16BeaconPeriod;
		pNewJoinBssParam->cap_info = ptstrNetworkInfo->u16CapInfo;
		memcpy(pNewJoinBssParam->au8bssid, ptstrNetworkInfo->au8bssid, 6);
		/*for(i=0; i<6;i++)
		 *      PRINT_D(HOSTINF_DBG,"%c",pNewJoinBssParam->au8bssid[i]);*/
		memcpy((u8 *)pNewJoinBssParam->ssid, ptstrNetworkInfo->au8ssid, ptstrNetworkInfo->u8SsidLen + 1);
		pNewJoinBssParam->ssidLen = ptstrNetworkInfo->u8SsidLen;
		memset(pNewJoinBssParam->rsn_pcip_policy, 0xFF, 3);
		memset(pNewJoinBssParam->rsn_auth_policy, 0xFF, 3);
		/*for(i=0; i<pNewJoinBssParam->ssidLen;i++)
		 *      PRINT_D(HOSTINF_DBG,"%c",pNewJoinBssParam->ssid[i]);*/

		/* parse supported rates: */
		while (index < u16IEsLen) {
			/* supportedRates IE */
			if (pu8IEs[index] == SUPP_RATES_IE) {
				/* PRINT_D(HOSTINF_DBG, "Supported Rates\n"); */
				suppRatesNo = pu8IEs[index + 1];
				pNewJoinBssParam->supp_rates[0] = suppRatesNo;
				index += 2; /* skipping ID and length bytes; */

				for (i = 0; i < suppRatesNo; i++) {
					pNewJoinBssParam->supp_rates[i + 1] = pu8IEs[index + i];
					/* PRINT_D(HOSTINF_DBG,"%0x ",pNewJoinBssParam->supp_rates[i+1]); */
				}
				index += suppRatesNo;
				continue;
			}
			/* Ext SupportedRates IE */
			else if (pu8IEs[index] == EXT_SUPP_RATES_IE) {
				/* PRINT_D(HOSTINF_DBG, "Extended Supported Rates\n"); */
				/* checking if no of ext. supp and supp rates < max limit */
				extSuppRatesNo = pu8IEs[index + 1];
				if (extSuppRatesNo > (MAX_RATES_SUPPORTED - suppRatesNo))
					pNewJoinBssParam->supp_rates[0] = MAX_RATES_SUPPORTED;
				else
					pNewJoinBssParam->supp_rates[0] += extSuppRatesNo;
				index += 2;
				/* pNewJoinBssParam.supp_rates[0] contains now old number not the ext. no */
				for (i = 0; i < (pNewJoinBssParam->supp_rates[0] - suppRatesNo); i++) {
					pNewJoinBssParam->supp_rates[suppRatesNo + i + 1] = pu8IEs[index + i];
					/* PRINT_D(HOSTINF_DBG,"%0x ",pNewJoinBssParam->supp_rates[suppRatesNo+i+1]); */
				}
				index += extSuppRatesNo;
				continue;
			}
			/* HT Cap. IE */
			else if (pu8IEs[index] == HT_CAPABILITY_IE) {
				/* if IE found set the flag */
				pNewJoinBssParam->ht_capable = true;
				index += pu8IEs[index + 1] + 2; /* ID,Length bytes and IE body */
				/* PRINT_D(HOSTINF_DBG,"HT_CAPABALE\n"); */
				continue;
			} else if ((pu8IEs[index] == WMM_IE) && /* WMM Element ID */
				   (pu8IEs[index + 2] == 0x00) && (pu8IEs[index + 3] == 0x50) &&
				   (pu8IEs[index + 4] == 0xF2) && /* OUI */
				   (pu8IEs[index + 5] == 0x02) && /* OUI Type     */
				   ((pu8IEs[index + 6] == 0x00) || (pu8IEs[index + 6] == 0x01)) && /* OUI Sub Type */
				   (pu8IEs[index + 7] == 0x01)) {
				/* Presence of WMM Info/Param element indicates WMM capability */
				pNewJoinBssParam->wmm_cap = true;

				/* Check if Bit 7 is set indicating U-APSD capability */
				if (pu8IEs[index + 8] & BIT(7))
					pNewJoinBssParam->uapsd_cap = true;
				index += pu8IEs[index + 1] + 2;
				continue;
			}
			else if ((pu8IEs[index] == P2P_IE) && /* P2P Element ID */
				 (pu8IEs[index + 2] == 0x50) && (pu8IEs[index + 3] == 0x6f) &&
				 (pu8IEs[index + 4] == 0x9a) && /* OUI */
				 (pu8IEs[index + 5] == 0x09) && (pu8IEs[index + 6] == 0x0c)) { /* OUI Type     */
				u16 u16P2P_count;

				pNewJoinBssParam->tsf = ptstrNetworkInfo->u32Tsf;
				pNewJoinBssParam->u8NoaEnbaled = 1;
				pNewJoinBssParam->u8Index = pu8IEs[index + 9];

				/* Check if Bit 7 is set indicating Opss capability */
				if (pu8IEs[index + 10] & BIT(7)) {
					pNewJoinBssParam->u8OppEnable = 1;
					pNewJoinBssParam->u8CtWindow = pu8IEs[index + 10];
				} else
					pNewJoinBssParam->u8OppEnable = 0;
				/* HOSTINF_DBG */
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

			}
			else if ((pu8IEs[index] == RSN_IE) ||
				 ((pu8IEs[index] == WPA_IE) && (pu8IEs[index + 2] == 0x00) &&
				  (pu8IEs[index + 3] == 0x50) && (pu8IEs[index + 4] == 0xF2) &&
				  (pu8IEs[index + 5] == 0x01)))	{
				u16 rsnIndex = index;
				/*PRINT_D(HOSTINF_DBG,"RSN IE Length:%d\n",pu8IEs[rsnIndex+1]);
				 * for(i=0; i<pu8IEs[rsnIndex+1]; i++)
				 * {
				 *      PRINT_D(HOSTINF_DBG,"%0x ",pu8IEs[rsnIndex+2+i]);
				 * }*/
				if (pu8IEs[rsnIndex] == RSN_IE)	{
					pNewJoinBssParam->mode_802_11i = 2;
					/* PRINT_D(HOSTINF_DBG,"\nRSN_IE\n"); */
				} else { /* check if rsn was previously parsed */
					if (pNewJoinBssParam->mode_802_11i == 0)
						pNewJoinBssParam->mode_802_11i = 1;
					/* PRINT_D(HOSTINF_DBG,"\nWPA_IE\n"); */
					rsnIndex += 4;
				}
				rsnIndex += 7; /* skipping id, length, version(2B) and first 3 bytes of gcipher */
				pNewJoinBssParam->rsn_grp_policy = pu8IEs[rsnIndex];
				rsnIndex++;
				/* PRINT_D(HOSTINF_DBG,"Group Policy: %0x\n",pNewJoinBssParam->rsn_grp_policy); */
				/* initialize policies with invalid values */

				jumpOffset = pu8IEs[rsnIndex] * 4; /* total no.of bytes of pcipher field (count*4) */

				/*parsing pairwise cipher*/

				/* saving 3 pcipher max. */
				pcipherCount = (pu8IEs[rsnIndex] > 3) ? 3 : pu8IEs[rsnIndex];
				rsnIndex += 2; /* jump 2 bytes of pcipher count */

				/* PRINT_D(HOSTINF_DBG,"\npcipher:%d\n",pcipherCount); */
				for (i = pcipherTotalCount, j = 0; i < pcipherCount + pcipherTotalCount && i < 3; i++, j++) {
					/* each count corresponds to 4 bytes, only last byte is saved */
					pNewJoinBssParam->rsn_pcip_policy[i] = pu8IEs[rsnIndex + ((j + 1) * 4) - 1];
					/* PRINT_D(HOSTINF_DBG,"PAIR policy = [%0x,%0x]\n",pNewJoinBssParam->rsn_pcip_policy[i],i); */
				}
				pcipherTotalCount += pcipherCount;
				rsnIndex += jumpOffset;

				jumpOffset = pu8IEs[rsnIndex] * 4;

				/*parsing AKM suite (auth_policy)*/
				/* saving 3 auth policies max. */
				authCount = (pu8IEs[rsnIndex] > 3) ? 3 : pu8IEs[rsnIndex];
				rsnIndex += 2; /* jump 2 bytes of pcipher count */

				for (i = authTotalCount, j = 0; i < authTotalCount + authCount; i++, j++) {
					/* each count corresponds to 4 bytes, only last byte is saved */
					pNewJoinBssParam->rsn_auth_policy[i] = pu8IEs[rsnIndex + ((j + 1) * 4) - 1];
				}
				authTotalCount += authCount;
				rsnIndex += jumpOffset;
				/*pasring rsn cap. only if rsn IE*/
				if (pu8IEs[index] == RSN_IE) {
					pNewJoinBssParam->rsn_cap[0] = pu8IEs[rsnIndex];
					pNewJoinBssParam->rsn_cap[1] = pu8IEs[rsnIndex + 1];
					rsnIndex += 2;
				}
				pNewJoinBssParam->rsn_found = true;
				index += pu8IEs[index + 1] + 2; /* ID,Length bytes and IE body */
				continue;
			} else
				index += pu8IEs[index + 1] + 2;  /* ID,Length bytes and IE body */

		}


	}

	return (void *)pNewJoinBssParam;

}

void host_int_freeJoinParams(void *pJoinParams)
{
	if ((tstrJoinBssParam *)pJoinParams != NULL)
		kfree((tstrJoinBssParam *)pJoinParams);
	else
		PRINT_ER("Unable to FREE null pointer\n");
}

/**
 *  @brief              host_int_addBASession
 *  @details            Open a block Ack session with the given parameters
 *  @param[in]          tstrNetworkInfo* ptstrNetworkInfo
 *  @return
 *  @author		anoureldin
 *  @date
 *  @version		1.0**/

static int host_int_addBASession(tstrWILC_WFIDrv *hWFIDrv, char *pBSSID, char TID, short int BufferSize,
				 short int SessionTimeout, void *drvHandler)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	struct ba_session_info *pBASessionInfo = &msg.body.session_info;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_ADD_BA_SESSION;

	memcpy(pBASessionInfo->au8Bssid, pBSSID, ETH_ALEN);
	pBASessionInfo->u8Ted = TID;
	pBASessionInfo->u16BufferSize = BufferSize;
	pBASessionInfo->u16SessionTimeout = SessionTimeout;
	msg.drvHandler = hWFIDrv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");

	return s32Error;
}


s32 host_int_delBASession(tstrWILC_WFIDrv *hWFIDrv, char *pBSSID, char TID)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	struct ba_session_info *pBASessionInfo = &msg.body.session_info;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_DEL_BA_SESSION;

	memcpy(pBASessionInfo->au8Bssid, pBSSID, ETH_ALEN);
	pBASessionInfo->u8Ted = TID;
	msg.drvHandler = hWFIDrv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");

	down(&hWaitResponse);

	return s32Error;
}

s32 host_int_del_All_Rx_BASession(tstrWILC_WFIDrv *hWFIDrv, char *pBSSID, char TID)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;
	struct ba_session_info *pBASessionInfo = &msg.body.session_info;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_DEL_ALL_RX_BA_SESSIONS;

	memcpy(pBASessionInfo->au8Bssid, pBSSID, ETH_ALEN);
	pBASessionInfo->u8Ted = TID;
	msg.drvHandler = hWFIDrv;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");

	down(&hWaitResponse);

	return s32Error;
}

/**
 *  @brief              host_int_setup_ipaddress
 *  @details            setup IP in firmware
 *  @param[in]          Handle to wifi driver
 *  @return             Error code.
 *  @author		Abdelrahman Sobhy
 *  @date
 *  @version		1.0*/
s32 host_int_setup_ipaddress(tstrWILC_WFIDrv *hWFIDrv, u8 *u16ipadd, u8 idx)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;

	/* TODO: Enable This feature on softap firmware */
	return 0;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_SET_IPADDRESS;

	msg.body.ip_info.au8IPAddr = u16ipadd;
	msg.drvHandler = hWFIDrv;
	msg.body.ip_info.idx = idx;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");

	return s32Error;


}

/**
 *  @brief              host_int_get_ipaddress
 *  @details            Get IP from firmware
 *  @param[in]          Handle to wifi driver
 *  @return             Error code.
 *  @author		Abdelrahman Sobhy
 *  @date
 *  @version		1.0*/
s32 host_int_get_ipaddress(tstrWILC_WFIDrv *hWFIDrv, u8 *u16ipadd, u8 idx)
{
	s32 s32Error = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	struct host_if_msg msg;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("driver is null\n");
		return -EFAULT;
	}

	memset(&msg, 0, sizeof(struct host_if_msg));

	/* prepare the WiphyParams Message */
	msg.id = HOST_IF_MSG_GET_IPADDRESS;

	msg.body.ip_info.au8IPAddr = u16ipadd;
	msg.drvHandler = hWFIDrv;
	msg.body.ip_info.idx = idx;

	s32Error = wilc_mq_send(&gMsgQHostIF, &msg, sizeof(struct host_if_msg));
	if (s32Error)
		PRINT_ER("wilc_mq_send fail\n");

	return s32Error;


}
