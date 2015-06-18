#include "host_interface.h"
#include "coreconfigurator.h"

extern s32 TransportInit(void);
extern s32 TransportDeInit(void);
extern u8 connecting;

#ifdef DISABLE_PWRSAVE_AND_SCAN_DURING_IP
extern WILC_TimerHandle hDuringIpTimer;
#endif

extern bool bEnablePS;
/*BugID_5137*/
extern u8 g_wilc_initialized;
/*****************************************************************************/
/*								Macros                                       */
/*****************************************************************************/

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
/*****************************************************************************/
/*								Type Definitions							 */
/*****************************************************************************/

/*!
 *  @struct             tstrHostIFCfgParamAttr
 *  @brief		Structure to hold Host IF CFG Params Attributes
 *  @details
 *  @todo
 *  @sa
 *  @author		Mai Daftedar
 *  @date		02 April 2012
 *  @version		1.0
 */
typedef struct _tstrHostIFCfgParamAttr {
	tstrCfgParamVal pstrCfgParamVal;

} tstrHostIFCfgParamAttr;

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
 *  @struct             tstrHostIFkeyAttr
 *  @brief		Structure to hold Host IF Scan Attributes
 *  @details
 *  @todo
 *  @sa
 *  @author		Mai Daftedar
 *  @date		25 March 2012
 *  @version		1.0
 */
typedef struct _tstrHostIFkeyAttr {
	tenuKeyType enuKeyType;
	u8 u8KeyAction;
	tuniHostIFkeyAttr uniHostIFkeyAttr;
} tstrHostIFkeyAttr;




/*!
 *  @struct             tstrHostIFscanAttr
 *  @brief		Structure to hold Host IF Scan Attributes
 *  @details
 *  @todo
 *  @sa
 *  @author		Mostafa Abu Bakr
 *  @date		25 March 2012
 *  @version		1.0
 */
typedef struct _tstrHostIFscanAttr {
	u8 u8ScanSource;
	u8 u8ScanType;
	u8 *pu8ChnlFreqList;
	u8 u8ChnlListLen;
	u8 *pu8IEs;
	size_t IEsLen;
	tWILCpfScanResult pfScanResult;
	void *pvUserArg;
	/*BugID_4189*/
	tstrHiddenNetwork strHiddenNetwork;

} tstrHostIFscanAttr;

/*!
 *  @struct             tstrHostIFconnectAttr
 *  @brief		Structure to hold Host IF Connect Attributes
 *  @details
 *  @todo
 *  @sa
 *  @author		Mostafa Abu Bakr
 *  @date		25 March 2012
 *  @version		1.0
 */
typedef struct _tstrHostIFconnectAttr {
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
} tstrHostIFconnectAttr;

/*!
 *  @struct             tstrRcvdGnrlAsyncInfo
 *  @brief		Structure to hold Received General Asynchronous info
 *  @details
 *  @todo
 *  @sa
 *  @author		Mostafa Abu Bakr
 *  @date		25 March 2012
 *  @version		1.0
 */
typedef struct _tstrRcvdGnrlAsyncInfo {
	u8 *pu8Buffer;
	u32 u32Length;
} tstrRcvdGnrlAsyncInfo;

/*!
 *  @struct             tstrHostIFSetChan
 *  @brief		Set Channel  message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Mai Daftedar
 *  @date		25 March 2012
 *  @version		1.0
 */
typedef struct _tstrHostIFSetChan {
	u8 u8SetChan;
} tstrHostIFSetChan;

/*!
 *  @struct             tstrHostIFSetChan
 *  @brief		Get Channel  message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Mai Daftedar
 *  @date		01 Jule 2012
 *  @version		1.0
 */
typedef struct _tstrHostIFGetChan {
	u8 u8GetChan;
} tstrHostIFGetChan;

/*bug3819: Add Scan acomplete notification to host*/
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
 *  @struct             tstrHostIFSetBeacon
 *  @brief		Set Beacon  message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Adham Abozaeid
 *  @date		10 July 2012
 *  @version		1.0
 */
typedef struct _tstrHostIFSetBeacon {
	u32 u32Interval;                        /*!< Beacon Interval. Period between two successive beacons on air  */
	u32 u32DTIMPeriod;              /*!< DTIM Period. Indicates how many Beacon frames
											*                              (including the current frame) appear before the next DTIM		*/
	u32 u32HeadLen;                         /*!< Length of the head buffer in bytes		*/
	u8 *pu8Head;                    /*!< Pointer to the beacon's head buffer. Beacon's head	is the part
											*              from the beacon's start till the TIM element, NOT including the TIM		*/
	u32 u32TailLen;                         /*!< Length of the tail buffer in bytes	*/
	u8 *pu8Tail;                    /*!< Pointer to the beacon's tail buffer. Beacon's tail	starts just
											*                              after the TIM inormation element	*/
} tstrHostIFSetBeacon;



/*!
 *  @struct             tstrHostIFDelBeacon
 *  @brief		Del Beacon  message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Adham Abozaeid
 *  @date		15 July 2012
 *  @version		1.0
 */
typedef struct _tstrHostIFDelBeacon {
	u8 u8dummy;
} tstrHostIFDelBeacon;

/*!
 *  @struct             tstrHostIFSetMulti
 *  @brief		set Multicast filter Address
 *  @details
 *  @todo
 *  @sa
 *  @author		Abdelrahman Sobhy
 *  @date		30 August 2013
 *  @version		1.0 Description
 */

typedef struct {
	bool bIsEnabled;
	u32 u32count;
} tstrHostIFSetMulti;

/*!
 *  @struct             tstrHostIFDelAllSta
 *  @brief		Deauth station message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Mai Daftedar
 *  @date		09 April 2014
 *  @version		1.0 Description
 */

typedef struct {
	u8 au8Sta_DelAllSta[MAX_NUM_STA][ETH_ALEN];
	u8 u8Num_AssocSta;
} tstrHostIFDelAllSta;

/*!
 *  @struct             tstrHostIFDelSta
 *  @brief		Delete station message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Adham Abozaeid
 *  @date		15 July 2012
 *  @version		1.0 Description
 */

typedef struct {
	u8 au8MacAddr[ETH_ALEN];
} tstrHostIFDelSta;

/*!
 *  @struct             tstrTimerCb
 *  @brief		Timer callback message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Mostafa Abu Bakr
 *  @date		25 March 2012
 *  @version		1.0
 */
typedef struct _tstrTimerCb {
	void *pvUsrArg;                 /*!< Private data passed at timer start */
} tstrTimerCb;

/*!
 *  @struct     tstrHostIfPowerMgmtParam
 *  @brief		Power management message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Adham Abozaeid
 *  @date		24 November 2012
 *  @version		1.0
 */
typedef struct {

	bool bIsEnabled;
	u32 u32Timeout;
} tstrHostIfPowerMgmtParam;

/*!
 *  @struct             tstrHostIFSetIPAddr
 *  @brief		set IP Address message body
 *  @details
 *  @todo
 *  @sa
 *  @author		Abdelrahman Sobhy
 *  @date		30 August 2013
 *  @version		1.0 Description
 */

typedef struct {
	u8 *au8IPAddr;
	u8 idx;
} tstrHostIFSetIPAddr;

/*!
 *  @struct     tstrHostIfStaInactiveT
 *  @brief		Get station message body
 *  @details
 *  @todo
 *  @sa
 *  @author	    Mai Daftedar
 *  @date		16 April 2013
 *  @version		1.0
 */
typedef struct {
	u8 mac[6];

} tstrHostIfStaInactiveT;
/**/
/*!
 *  @union              tuniHostIFmsgBody
 *  @brief		Message body for the Host Interface message_q
 *  @details
 *  @todo
 *  @sa
 *  @author		Mostafa Abu Bakr
 *  @date		25 March 2012
 *  @version		1.0
 */
typedef union _tuniHostIFmsgBody {
	tstrHostIFscanAttr strHostIFscanAttr;                           /*!< Host IF Scan Request Attributes message body */
	tstrHostIFconnectAttr strHostIFconnectAttr;     /*!< Host IF Connect Request Attributes message body */
	tstrRcvdNetworkInfo strRcvdNetworkInfo;                 /*!< Received Asynchronous Network Info message body */
	tstrRcvdGnrlAsyncInfo strRcvdGnrlAsyncInfo;     /*!< Received General Asynchronous Info message body */
	tstrHostIFkeyAttr strHostIFkeyAttr;                             /*!<>*/
	tstrHostIFCfgParamAttr strHostIFCfgParamAttr;            /*! <CFG Parameter message Body> */
	tstrHostIFSetChan strHostIFSetChan;
	tstrHostIFGetChan strHostIFGetChan;
	tstrHostIFSetBeacon strHostIFSetBeacon;                 /*!< Set beacon message body */
	tstrHostIFDelBeacon strHostIFDelBeacon;                 /*!< Del beacon message body */
	tstrWILC_AddStaParam strAddStaParam;                    /*!< Add station message body */
	tstrHostIFDelSta strDelStaParam;                                /*!< Del Station message body */
	tstrWILC_AddStaParam strEditStaParam;                           /*!< Edit station message body */
	/* tstrScanComplete		strScanComplete;		/ *Received Async. Scan Complete message body* / */
	tstrTimerCb strTimerCb;                                                 /*!< Timer callback message body */
	tstrHostIfPowerMgmtParam strPowerMgmtparam;     /*!< Power Management message body */
	tstrHostIfStaInactiveT strHostIfStaInactiveT;
	tstrHostIFSetIPAddr strHostIfSetIP;
	tstrHostIfSetDrvHandler strHostIfSetDrvHandler;
	tstrHostIFSetMulti strHostIfSetMulti;
	tstrHostIfSetOperationMode strHostIfSetOperationMode;
	tstrHostIfSetMacAddress strHostIfSetMacAddress;
	tstrHostIfGetMacAddress strHostIfGetMacAddress;
	tstrHostIfBASessionInfo strHostIfBASessionInfo;
	#ifdef WILC_P2P
	tstrHostIfRemainOnChan strHostIfRemainOnChan;
	tstrHostIfRegisterFrame strHostIfRegisterFrame;
	#endif
	char *pUserData;
	tstrHostIFDelAllSta strHostIFDelAllSta;
} tuniHostIFmsgBody;

/*!
 *  @struct             tstrHostIFmsg
 *  @brief		Host Interface message
 *  @details
 *  @todo
 *  @sa
 *  @author		Mostafa Abu Bakr
 *  @date		25 March 2012
 *  @version		1.0
 */
typedef struct _tstrHostIFmsg {
	u16 u16MsgId;                                           /*!< Message ID */
	tuniHostIFmsgBody uniHostIFmsgBody;             /*!< Message body */
	void *drvHandler;
} tstrHostIFmsg;

#ifdef CONNECT_DIRECT
typedef struct _tstrWidJoinReqExt {
	char SSID[MAX_SSID_LEN];
	u8 u8channel;
	u8 BSSID[6];
} tstrWidJoinReqExt;
#endif

/*Bug4218: Parsing Join Param*/
#ifdef WILC_PARSE_SCAN_IN_HOST
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
	#ifdef WILC_P2P
	u32 tsf;
	u8 u8NoaEnbaled;
	u8 u8OppEnable;
	u8 u8CtWindow;
	u8 u8Count;
	u8 u8Index;
	u8 au8Duration[4];
	u8 au8Interval[4];
	u8 au8StartTime[4];
	#endif
} tstrJoinBssParam;
/*Bug4218: Parsing Join Param*/
/*a linked list table containing needed join parameters entries for each AP found in most recent scan*/
typedef struct _tstrBssTable {
	u8 u8noBssEntries;
	tstrJoinBssParam *head;
	tstrJoinBssParam *tail;
} tstrBssTable;
#endif /*WILC_PARSE_SCAN_IN_HOST*/

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


tstrWILC_WFIDrv *terminated_handle = NULL;
tstrWILC_WFIDrv *gWFiDrvHandle = NULL;
#ifdef DISABLE_PWRSAVE_AND_SCAN_DURING_IP
bool g_obtainingIP = false;
#endif
u8 P2P_LISTEN_STATE;
static struct task_struct *HostIFthreadHandler;
static WILC_MsgQueueHandle gMsgQHostIF;
static struct semaphore hSemHostIFthrdEnd;

struct semaphore hSemDeinitDrvHandle;
static struct semaphore hWaitResponse;
struct semaphore hSemHostIntDeinit;
WILC_TimerHandle g_hPeriodicRSSI;



u8 gau8MulticastMacAddrList[WILC_MULTICAST_TABLE_SIZE][ETH_ALEN];

#ifndef CONNECT_DIRECT
static u8 gapu8RcvdSurveyResults[2][MAX_SURVEY_RESULT_FRAG_SIZE];
#endif

static u8 gapu8RcvdAssocResp[MAX_ASSOC_RESP_FRAME_SIZE];

bool gbScanWhileConnected = false;

static s8 gs8Rssi;
static s8 gs8lnkspd;
static u8 gu8Chnl;
static u8 gs8SetIP[2][4];
static u8 gs8GetIP[2][4];
#ifdef WILC_AP_EXTERNAL_MLME
static u32 gu32InactiveTime;
static u8 gu8DelBcn;
#endif
#ifndef SIMULATION
static u32 gu32WidConnRstHack;
#endif

/*BugID_5137*/
u8 *gu8FlushedJoinReq;
u8 *gu8FlushedInfoElemAsoc;
u8 gu8Flushed11iMode;
u8 gu8FlushedAuthType;
u32 gu32FlushedJoinReqSize;
u32 gu32FlushedInfoElemAsocSize;
u32 gu8FlushedJoinReqDrvHandler;
#define REAL_JOIN_REQ 0
#define FLUSHED_JOIN_REQ 1
#define FLUSHED_BYTE_POS 79     /* Position the byte indicating flushing in the flushed request */

/*Bug4218: Parsing Join Param*/
#ifdef WILC_PARSE_SCAN_IN_HOST
/*Bug4218: Parsing Join Param*/
static void *host_int_ParseJoinBssParam(tstrNetworkInfo *ptstrNetworkInfo);
#endif /*WILC_PARSE_SCAN_IN_HOST*/

extern void chip_sleep_manually(u32 u32SleepTime);
extern int linux_wlan_get_num_conn_ifcs(void);

/**
 *  @brief Handle_SetChannel
 *  @details    Sending config packet to firmware to set channel
 *  @param[in]   tstrHostIFSetChan* pstrHostIFSetChan
 *  @return     Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_SetChannel(void *drvHandler, tstrHostIFSetChan *pstrHostIFSetChan)
{

	s32 s32Error = WILC_SUCCESS;
	tstrWID	strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_CURRENT_CHANNEL;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = (char *)&(pstrHostIFSetChan->u8SetChan);
	strWID.s32ValueSize = sizeof(char);

	PRINT_D(HOSTINF_DBG, "Setting channel\n");
	/*Sending Cfg*/
	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	if (s32Error) {
		PRINT_ER("Failed to set channel\n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
	}
	WILC_CATCH(s32Error)
	{

	}

	return s32Error;
}
/**
 *  @brief Handle_SetWfiDrvHandler
 *  @details    Sending config packet to firmware to set driver handler
 *  @param[in]   void * drvHandler,tstrHostIfSetDrvHandler* pstrHostIfSetDrvHandler
 *  @return     Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_SetWfiDrvHandler(tstrHostIfSetDrvHandler *pstrHostIfSetDrvHandler)
{

	s32 s32Error = WILC_SUCCESS;
	tstrWID	strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)((pstrHostIfSetDrvHandler->u32Address));


	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_SET_DRV_HANDLER;
	strWID.enuWIDtype = WID_INT;
	strWID.ps8WidVal = (s8 *)&(pstrHostIfSetDrvHandler->u32Address);
	strWID.s32ValueSize = sizeof(u32);

	/*Sending Cfg*/

	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);


	if ((pstrHostIfSetDrvHandler->u32Address) == (u32)NULL) {
		up(&hSemDeinitDrvHandle);
	}


	if (s32Error) {
		PRINT_ER("Failed to set driver handler\n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
	}
	WILC_CATCH(s32Error)
	{

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
static s32 Handle_SetOperationMode(void *drvHandler, tstrHostIfSetOperationMode *pstrHostIfSetOperationMode)
{

	s32 s32Error = WILC_SUCCESS;
	tstrWID	strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;


	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_SET_OPERATION_MODE;
	strWID.enuWIDtype = WID_INT;
	strWID.ps8WidVal = (s8 *)&(pstrHostIfSetOperationMode->u32Mode);
	strWID.s32ValueSize = sizeof(u32);

	/*Sending Cfg*/
	PRINT_INFO(HOSTINF_DBG, "pstrWFIDrv= %p \n", pstrWFIDrv);

	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);


	if ((pstrHostIfSetOperationMode->u32Mode) == (u32)NULL) {
		up(&hSemDeinitDrvHandle);
	}


	if (s32Error) {
		PRINT_ER("Failed to set driver handler\n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
	}
	WILC_CATCH(s32Error)
	{

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
s32 Handle_set_IPAddress(void *drvHandler, u8 *pu8IPAddr, u8 idx)
{

	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	char firmwareIPAddress[4] = {0};
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	if (pu8IPAddr[0] < 192)
		pu8IPAddr[0] = 0;

	PRINT_INFO(HOSTINF_DBG, "Indx = %d, Handling set  IP = %d.%d.%d.%d \n", idx, pu8IPAddr[0], pu8IPAddr[1], pu8IPAddr[2], pu8IPAddr[3]);

	WILC_memcpy(gs8SetIP[idx], pu8IPAddr, IP_ALEN);

	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_IP_ADDRESS;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = (u8 *)pu8IPAddr;
	strWID.s32ValueSize = IP_ALEN;

	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);



	host_int_get_ipaddress((WILC_WFIDrvHandle)drvHandler, firmwareIPAddress, idx);

	if (s32Error) {
		PRINT_D(HOSTINF_DBG, "Failed to set IP address\n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
	} else {
		PRINT_INFO(HOSTINF_DBG, "IP address set\n");
	}

	WILC_CATCH(s32Error)
	{

	}

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
s32 Handle_get_IPAddress(void *drvHandler, u8 *pu8IPAddr, u8 idx)
{

	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_IP_ADDRESS;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = (u8 *)WILC_MALLOC(IP_ALEN);
	strWID.s32ValueSize = IP_ALEN;

	s32Error = SendConfigPkt(GET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);

	PRINT_INFO(HOSTINF_DBG, "%d.%d.%d.%d\n", (u8)(strWID.ps8WidVal[0]), (u8)(strWID.ps8WidVal[1]), (u8)(strWID.ps8WidVal[2]), (u8)(strWID.ps8WidVal[3]));

	WILC_memcpy(gs8GetIP[idx], strWID.ps8WidVal, IP_ALEN);

	/*get the value by searching the local copy*/
	WILC_FREE(strWID.ps8WidVal);

	if (WILC_memcmp(gs8GetIP[idx], gs8SetIP[idx], IP_ALEN) != 0)
		host_int_setup_ipaddress((WILC_WFIDrvHandle)pstrWFIDrv, gs8SetIP[idx], idx);

	if (s32Error != WILC_SUCCESS) {
		PRINT_ER("Failed to get IP address\n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
	} else {
		PRINT_INFO(HOSTINF_DBG, "IP address retrieved:: u8IfIdx = %d \n", idx);
		PRINT_INFO(HOSTINF_DBG, "%d.%d.%d.%d\n", gs8GetIP[idx][0], gs8GetIP[idx][1], gs8GetIP[idx][2], gs8GetIP[idx][3]);
		PRINT_INFO(HOSTINF_DBG, "\n");
	}

	WILC_CATCH(s32Error)
	{

	}

	return s32Error;
}


/*BugId_5077*/
/**
 *  @brief Handle_SetMacAddress
 *  @details    Setting mac address
 *  @param[in]   void * drvHandler,tstrHostIfSetDrvHandler* pstrHostIfSetDrvHandler
 *  @return     Error code.
 *  @author	Amr Abdel-Moghny
 *  @date		November 2013
 *  @version	7.0
 */
static s32 Handle_SetMacAddress(void *drvHandler, tstrHostIfSetMacAddress *pstrHostIfSetMacAddress)
{

	s32 s32Error = WILC_SUCCESS;
	tstrWID	strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;
	u8 *mac_buf = (u8 *)WILC_MALLOC(ETH_ALEN);
	if (mac_buf == NULL) {
		PRINT_ER("No buffer to send mac address\n");
		return WILC_FAIL;
	}
	WILC_memcpy(mac_buf, pstrHostIfSetMacAddress->u8MacAddress, ETH_ALEN);

	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_MAC_ADDR;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = mac_buf;
	strWID.s32ValueSize = ETH_ALEN;
	PRINT_D(GENERIC_DBG, "mac addr = :%x:%x:%x:%x:%x:%x\n", strWID.ps8WidVal[0], strWID.ps8WidVal[1], strWID.ps8WidVal[2], strWID.ps8WidVal[3], strWID.ps8WidVal[4], strWID.ps8WidVal[5]);
	/*Sending Cfg*/
	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	if (s32Error) {
		PRINT_ER("Failed to set mac address\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	WILC_CATCH(s32Error)
	{

	}
	WILC_FREE(mac_buf);
	return s32Error;
}


/*BugID_5213*/
/**
 *  @brief Handle_GetMacAddress
 *  @details    Getting mac address
 *  @param[in]   void * drvHandler,tstrHostIfSetDrvHandler* pstrHostIfSetDrvHandler
 *  @return     Error code.
 *  @author	Amr Abdel-Moghny
 *  @date		JAN 2013
 *  @version	8.0
 */
static s32 Handle_GetMacAddress(void *drvHandler, tstrHostIfGetMacAddress *pstrHostIfGetMacAddress)
{

	s32 s32Error = WILC_SUCCESS;
	tstrWID	strWID;

	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_MAC_ADDR;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = pstrHostIfGetMacAddress->u8MacAddress;
	strWID.s32ValueSize = ETH_ALEN;

	/*Sending Cfg*/
	s32Error = SendConfigPkt(GET_CFG, &strWID, 1, false, (u32)drvHandler);
	if (s32Error) {
		PRINT_ER("Failed to get mac address\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}
	WILC_CATCH(s32Error)
	{

	}
	up(&hWaitResponse);

	return s32Error;
}


/**
 *  @brief Handle_CfgParam
 *  @details    Sending config packet to firmware to set CFG params
 *  @param[in]   tstrHostIFCfgParamAttr* strHostIFCfgParamAttr
 *  @return     Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_CfgParam(void *drvHandler, tstrHostIFCfgParamAttr *strHostIFCfgParamAttr)
{
	s32 s32Error = WILC_SUCCESS;
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
		}
		u8WidCnt++;
	}
	s32Error = SendConfigPkt(SET_CFG, strWIDList, u8WidCnt, false, (u32)pstrWFIDrv);

	if (s32Error) {
		PRINT_ER("Error in setting CFG params\n");

	}
	WILC_CATCH(s32Error)
	{
	}
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
	s32 s32Error = WILC_SUCCESS;
	g_wilc_initialized = 0;
	up(&hWaitResponse);
	return s32Error;
}

/**
 *  @brief Handle_Scan
 *  @details       Sending config packet to firmware to set the scan params
 *  @param[in]    tstrHostIFscanAttr* pstrHostIFscanAttr
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_Scan(void *drvHandler, tstrHostIFscanAttr *pstrHostIFscanAttr)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWIDList[5];
	u32 u32WidsCount = 0;
	u32 i;
	u8 *pu8Buffer;
	u8 valuesize = 0;
	u8 *pu8HdnNtwrksWidVal = NULL;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *) drvHandler;

	PRINT_D(HOSTINF_DBG, "Setting SCAN params\n");
	PRINT_D(HOSTINF_DBG, "Scanning: In [%d] state \n", pstrWFIDrv->enuHostIFstate);

	pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult = pstrHostIFscanAttr->pfScanResult;
	pstrWFIDrv->strWILC_UsrScanReq.u32UserScanPvoid = pstrHostIFscanAttr->pvUserArg;

	#ifdef WILC_P2P
	#endif

	if ((pstrWFIDrv->enuHostIFstate >= HOST_IF_SCANNING) && (pstrWFIDrv->enuHostIFstate < HOST_IF_CONNECTED)) {
		/* here we either in HOST_IF_SCANNING, HOST_IF_WAITING_CONN_REQ or HOST_IF_WAITING_CONN_RESP */
		PRINT_D(GENERIC_DBG, "Don't scan we are already in [%d] state\n", pstrWFIDrv->enuHostIFstate);
		WILC_ERRORREPORT(s32Error, WILC_BUSY);
	}

	#ifdef DISABLE_PWRSAVE_AND_SCAN_DURING_IP
	if (g_obtainingIP || connecting) {
		PRINT_D(GENERIC_DBG, "[handle_scan]: Don't do obss scan until IP adresss is obtained\n");
		WILC_ERRORREPORT(s32Error, WILC_BUSY);
	}
	#endif

	PRINT_D(HOSTINF_DBG, "Setting SCAN params\n");


	pstrWFIDrv->strWILC_UsrScanReq.u32RcvdChCount = 0;

	/*BugID_4189*/
	strWIDList[u32WidsCount].u16WIDid = (u16)WID_SSID_PROBE_REQ;
	strWIDList[u32WidsCount].enuWIDtype = WID_STR;

	for (i = 0; i < pstrHostIFscanAttr->strHiddenNetwork.u8ssidnum; i++) {
		valuesize += ((pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo[i].u8ssidlen) + 1);
	}
	pu8HdnNtwrksWidVal = WILC_MALLOC(valuesize + 1);
	strWIDList[u32WidsCount].ps8WidVal = pu8HdnNtwrksWidVal;
	if (strWIDList[u32WidsCount].ps8WidVal != NULL) {
		pu8Buffer = strWIDList[u32WidsCount].ps8WidVal;

		*pu8Buffer++ = pstrHostIFscanAttr->strHiddenNetwork.u8ssidnum;

		PRINT_D(HOSTINF_DBG, "In Handle_ProbeRequest number of ssid %d\n", pstrHostIFscanAttr->strHiddenNetwork.u8ssidnum);

		for (i = 0; i < pstrHostIFscanAttr->strHiddenNetwork.u8ssidnum; i++) {
			*pu8Buffer++ = pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo[i].u8ssidlen;
			WILC_memcpy(pu8Buffer, pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo[i].pu8ssid, pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo[i].u8ssidlen);
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

	/* Bug 4648: Convert channel numbers to start from 0 not 1. */
	if (pstrHostIFscanAttr->pu8ChnlFreqList != NULL && pstrHostIFscanAttr->u8ChnlListLen > 0) {
		int i;

		for (i = 0; i < pstrHostIFscanAttr->u8ChnlListLen; i++)	{
			if (pstrHostIFscanAttr->pu8ChnlFreqList[i] > 0) {
				pstrHostIFscanAttr->pu8ChnlFreqList[i] = pstrHostIFscanAttr->pu8ChnlFreqList[i] - 1;
			}
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

	if (pstrWFIDrv->enuHostIFstate == HOST_IF_CONNECTED) {
		gbScanWhileConnected = true;
	} else if (pstrWFIDrv->enuHostIFstate == HOST_IF_IDLE)	  {
		gbScanWhileConnected = false;
	}

	s32Error = SendConfigPkt(SET_CFG, strWIDList, u32WidsCount, false, (u32)pstrWFIDrv);

	if (s32Error) {
		PRINT_ER("Failed to send scan paramters config packet\n");
		WILC_ERRORREPORT(s32Error, s32Error);
	} else {
		PRINT_D(HOSTINF_DBG, "Successfully sent SCAN params config packet\n");
	}

	WILC_CATCH(s32Error)
	{
		WILC_TimerStop(&(pstrWFIDrv->hScanTimer), NULL);
		/*if there is an ongoing scan request*/
		Handle_ScanDone(drvHandler, SCAN_EVENT_ABORTED);
	}

	/* Deallocate pstrHostIFscanAttr->u8ChnlListLen which was prevoisuly allocated by the sending thread */
	if (pstrHostIFscanAttr->pu8ChnlFreqList != NULL) {
		WILC_FREE(pstrHostIFscanAttr->pu8ChnlFreqList);
		pstrHostIFscanAttr->pu8ChnlFreqList = NULL;
	}

	/* Deallocate pstrHostIFscanAttr->pu8IEs which was previously allocated by the sending thread */
	if (pstrHostIFscanAttr->pu8IEs != NULL)	{
		WILC_FREE(pstrHostIFscanAttr->pu8IEs);
		pstrHostIFscanAttr->pu8IEs = NULL;
	}
	if (pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo != NULL)	{
		WILC_FREE(pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo);
		pstrHostIFscanAttr->strHiddenNetwork.pstrHiddenNetworkInfo = NULL;
	}

	/* Deallocate pstrHostIFscanAttr->u8ChnlListLen which was prevoisuly allocated by the sending thread */
	if (pstrHostIFscanAttr->pu8ChnlFreqList != NULL) {
		WILC_FREE(pstrHostIFscanAttr->pu8ChnlFreqList);
		pstrHostIFscanAttr->pu8ChnlFreqList = NULL;
	}

	if (pu8HdnNtwrksWidVal != NULL)	{
		WILC_FREE(pu8HdnNtwrksWidVal);
	}

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
static s32 Handle_ScanDone(void *drvHandler, tenuScanEvent enuEvent)
{
	s32 s32Error = WILC_SUCCESS;

	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;


	u8 u8abort_running_scan;
	tstrWID strWID;


	PRINT_D(HOSTINF_DBG, "in Handle_ScanDone()\n");

	/*BugID_4978*/
	/*Ask FW to abort the running scan, if any*/
	if (enuEvent == SCAN_EVENT_ABORTED) {
		PRINT_D(GENERIC_DBG, "Abort running scan\n");
		u8abort_running_scan = 1;
		strWID.u16WIDid	= (u16)WID_ABORT_RUNNING_SCAN;
		strWID.enuWIDtype	= WID_CHAR;
		strWID.ps8WidVal = (s8 *)&u8abort_running_scan;
		strWID.s32ValueSize = sizeof(char);

		/*Sending Cfg*/
		s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
		if (s32Error != WILC_SUCCESS) {
			PRINT_ER("Failed to set abort running scan\n");
			WILC_ERRORREPORT(s32Error, WILC_FAIL);
		}
		WILC_CATCH(s32Error)
		{
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
 *  @param[in]    tstrHostIFconnectAttr* pstrHostIFconnectAttr
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
u8 u8ConnectedSSID[6] = {0};
static s32 Handle_Connect(void *drvHandler, tstrHostIFconnectAttr *pstrHostIFconnectAttr)
{
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *) drvHandler;
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWIDList[8];
	u32 u32WidsCount = 0, dummyval = 0;
	/* char passphrase[] = "12345678"; */
	#ifndef CONNECT_DIRECT
	s32 s32Err = WILC_SUCCESS;
	u32 i;
	u8 u8bssDscListIndex;
	wid_site_survey_reslts_s *pstrSurveyResults = NULL;
	#else
	u8 *pu8CurrByte = NULL;
	/*Bug4218: Parsing Join Param*/
	#ifdef WILC_PARSE_SCAN_IN_HOST
	tstrJoinBssParam *ptstrJoinBssParam;
	#endif /*WILC_PARSE_SCAN_IN_HOST*/

	#endif

	PRINT_D(GENERIC_DBG, "Handling connect request\n");

	#ifndef CONNECT_DIRECT
	WILC_memset(gapu8RcvdSurveyResults[0], 0, MAX_SURVEY_RESULT_FRAG_SIZE);
	WILC_memset(gapu8RcvdSurveyResults[1], 0, MAX_SURVEY_RESULT_FRAG_SIZE);


	PRINT_D(HOSTINF_DBG, "Getting site survey results\n");
	s32Err = host_int_get_site_survey_results((WILC_WFIDrvHandle)pstrWFIDrv,
						  gapu8RcvdSurveyResults,
						  MAX_SURVEY_RESULT_FRAG_SIZE);
	if (s32Err) {
		PRINT_ER("Failed to get site survey results\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);

	}
	s32Err = ParseSurveyResults(gapu8RcvdSurveyResults, &pstrSurveyResults,
				    &pstrWFIDrv->u32SurveyResultsCount);


	if (s32Err == WILC_SUCCESS) {
		/* use the parsed info in pstrSurveyResults, then deallocate it */
		PRINT_D(HOSTINF_DBG, "Copying site survey results in global structure, then deallocate\n");
		for (i = 0; i < pstrWFIDrv->u32SurveyResultsCount; i++)	{
			WILC_memcpy(&pstrWFIDrv->astrSurveyResults[i], &pstrSurveyResults[i],
				    sizeof(wid_site_survey_reslts_s));
		}

		DeallocateSurveyResults(pstrSurveyResults);
	} else {
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
		PRINT_ER("ParseSurveyResults() Error(%d) \n", s32Err);
	}


	for (i = 0; i < pstrWFIDrv->u32SurveyResultsCount; i++)	{
		if (WILC_memcmp(pstrWFIDrv->astrSurveyResults[i].SSID,
				pstrHostIFconnectAttr->pu8ssid,
				pstrHostIFconnectAttr->ssidLen) == 0) {
			PRINT_INFO(HOSTINF_DBG, "Network with required SSID is found %s\n", pstrHostIFconnectAttr->pu8ssid);
			if (pstrHostIFconnectAttr->pu8bssid == NULL) {
				/* BSSID is not passed from the user, so decision of matching
				 * is done by SSID only */
				PRINT_INFO(HOSTINF_DBG, "BSSID is not passed from the user\n");
				break;
			} else {
				/* BSSID is also passed from the user, so decision of matching
				 * should consider also this passed BSSID */

				if (WILC_memcmp(pstrWFIDrv->astrSurveyResults[i].BSSID,
						pstrHostIFconnectAttr->pu8bssid,
						6) == 0) {
					PRINT_INFO(HOSTINF_DBG, "BSSID is passed from the user and matched\n");
					break;
				}
			}
		}
	}

	if (i < pstrWFIDrv->u32SurveyResultsCount) {
		u8bssDscListIndex = i;

		PRINT_INFO(HOSTINF_DBG, "Connecting to network of Bss Idx %d and SSID %s and channel %d \n",
			   u8bssDscListIndex, pstrWFIDrv->astrSurveyResults[u8bssDscListIndex].SSID,
			   pstrWFIDrv->astrSurveyResults[u8bssDscListIndex].Channel);

		PRINT_INFO(HOSTINF_DBG, "Saving connection parameters in global structure\n");

		if (pstrHostIFconnectAttr->pu8bssid != NULL) {
			pstrWFIDrv->strWILC_UsrConnReq.pu8bssid = (u8 *)WILC_MALLOC(6);
			WILC_memcpy(pstrWFIDrv->strWILC_UsrConnReq.pu8bssid, pstrHostIFconnectAttr->pu8bssid, 6);
		}

		pstrWFIDrv->strWILC_UsrConnReq.ssidLen = pstrHostIFconnectAttr->ssidLen;
		if (pstrHostIFconnectAttr->pu8ssid != NULL) {
			pstrWFIDrv->strWILC_UsrConnReq.pu8ssid = (u8 *)WILC_MALLOC(pstrHostIFconnectAttr->ssidLen + 1);
			WILC_memcpy(pstrWFIDrv->strWILC_UsrConnReq.pu8ssid, pstrHostIFconnectAttr->pu8ssid,
				    pstrHostIFconnectAttr->ssidLen);
			pstrWFIDrv->strWILC_UsrConnReq.pu8ssid[pstrHostIFconnectAttr->ssidLen] = '\0';
		}

		pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen = pstrHostIFconnectAttr->IEsLen;
		if (pstrHostIFconnectAttr->pu8IEs != NULL) {
			pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs = (u8 *)WILC_MALLOC(pstrHostIFconnectAttr->IEsLen);
			WILC_memcpy(pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs, pstrHostIFconnectAttr->pu8IEs,
				    pstrHostIFconnectAttr->IEsLen);
		}

		pstrWFIDrv->strWILC_UsrConnReq.u8security = pstrHostIFconnectAttr->u8security;
		pstrWFIDrv->strWILC_UsrConnReq.tenuAuth_type = pstrHostIFconnectAttr->tenuAuth_type;
		pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult = pstrHostIFconnectAttr->pfConnectResult;
		pstrWFIDrv->strWILC_UsrConnReq.u32UserConnectPvoid = pstrHostIFconnectAttr->pvUserArg;


		/* if((gWFiDrvHandle->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) && */
		/* (gWFiDrvHandle->strWILC_UsrConnReq.ConnReqIEsLen != 0)) */
		{
			/* IEs to be inserted in Association Request */
			strWIDList[u32WidsCount].u16WIDid = WID_INFO_ELEMENT_ASSOCIATE;
			strWIDList[u32WidsCount].enuWIDtype = WID_BIN_DATA;
			strWIDList[u32WidsCount].ps8WidVal = pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs;
			strWIDList[u32WidsCount].s32ValueSize = pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen;
			u32WidsCount++;
		}
		strWIDList[u32WidsCount].u16WIDid = (u16)WID_11I_MODE;
		strWIDList[u32WidsCount].enuWIDtype = WID_CHAR;
		strWIDList[u32WidsCount].s32ValueSize = sizeof(char);
		strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&(pstrWFIDrv->strWILC_UsrConnReq.u8security));
		u32WidsCount++;

		PRINT_INFO(HOSTINF_DBG, "Encrypt Mode = %x\n", pstrWFIDrv->strWILC_UsrConnReq.u8security);

		strWIDList[u32WidsCount].u16WIDid = (u16)WID_AUTH_TYPE;
		strWIDList[u32WidsCount].enuWIDtype = WID_CHAR;
		strWIDList[u32WidsCount].s32ValueSize = sizeof(char);
		strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&pstrWFIDrv->strWILC_UsrConnReq.tenuAuth_type);
		u32WidsCount++;

		PRINT_INFO(HOSTINF_DBG, "Authentication Type = %x\n", pstrWFIDrv->strWILC_UsrConnReq.tenuAuth_type);
		/*
		 * strWIDList[u32WidsCount].u16WIDid = (u16)WID_11I_PSK;
		 * strWIDList[u32WidsCount].enuWIDtype = WID_STR;
		 * strWIDList[u32WidsCount].s32ValueSize = sizeof(passphrase);
		 * strWIDList[u32WidsCount].ps8WidVal = (s8*)(passphrase);
		 * u32WidsCount++;
		 */

		strWIDList[u32WidsCount].u16WIDid = (u16)WID_JOIN_REQ;
		strWIDList[u32WidsCount].enuWIDtype = WID_CHAR;
		strWIDList[u32WidsCount].s32ValueSize = sizeof(char);
		strWIDList[u32WidsCount].ps8WidVal = (s8 *)&u8bssDscListIndex;
		u32WidsCount++;

		#ifndef SIMULATION
		/* A temporary workaround to avoid handling the misleading MAC_DISCONNECTED raised from the
		 *   firmware at chip reset when processing the WIDs of the Connect Request.
		 *   (This workaround should be removed in the future when the Chip reset of the Connect WIDs is disabled) */
		/* ////////////////////// */
		gu32WidConnRstHack = 0;
		/* ////////////////////// */
		#endif

		s32Error = SendConfigPkt(SET_CFG, strWIDList, u32WidsCount, false, (u32)pstrWFIDrv);
		if (s32Error) {
			PRINT_ER("Handle_Connect()] failed to send config packet\n");
			WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
		} else {
			pstrWFIDrv->enuHostIFstate = HOST_IF_WAITING_CONN_RESP;
		}

	} else {
		PRINT_ER("Required BSSID not found\n");
		WILC_ERRORREPORT(s32Error, WILC_NOT_FOUND);
	}

	#else

	/* if we try to connect to an already connected AP then discard the request */

	if (WILC_memcmp(pstrHostIFconnectAttr->pu8bssid, u8ConnectedSSID, ETH_ALEN) == 0) {

		s32Error = WILC_SUCCESS;
		PRINT_ER("Trying to connect to an already connected AP, Discard connect request\n");
		return s32Error;
	}

	PRINT_INFO(HOSTINF_DBG, "Saving connection parameters in global structure\n");

	/*Bug4218: Parsing Join Param*/
	#ifdef WILC_PARSE_SCAN_IN_HOST
	ptstrJoinBssParam = (tstrJoinBssParam *)pstrHostIFconnectAttr->pJoinParams;
	if (ptstrJoinBssParam == NULL) {
		PRINT_ER("Required BSSID not found\n");
		WILC_ERRORREPORT(s32Error, WILC_NOT_FOUND);
	}
	#endif /*WILC_PARSE_SCAN_IN_HOST*/

	if (pstrHostIFconnectAttr->pu8bssid != NULL) {
		pstrWFIDrv->strWILC_UsrConnReq.pu8bssid = (u8 *)WILC_MALLOC(6);
		WILC_memcpy(pstrWFIDrv->strWILC_UsrConnReq.pu8bssid, pstrHostIFconnectAttr->pu8bssid, 6);
	}

	pstrWFIDrv->strWILC_UsrConnReq.ssidLen = pstrHostIFconnectAttr->ssidLen;
	if (pstrHostIFconnectAttr->pu8ssid != NULL) {
		pstrWFIDrv->strWILC_UsrConnReq.pu8ssid = (u8 *)WILC_MALLOC(pstrHostIFconnectAttr->ssidLen + 1);
		WILC_memcpy(pstrWFIDrv->strWILC_UsrConnReq.pu8ssid, pstrHostIFconnectAttr->pu8ssid,
			    pstrHostIFconnectAttr->ssidLen);
		pstrWFIDrv->strWILC_UsrConnReq.pu8ssid[pstrHostIFconnectAttr->ssidLen] = '\0';
	}

	pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen = pstrHostIFconnectAttr->IEsLen;
	if (pstrHostIFconnectAttr->pu8IEs != NULL) {
		pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs = (u8 *)WILC_MALLOC(pstrHostIFconnectAttr->IEsLen);
		WILC_memcpy(pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs, pstrHostIFconnectAttr->pu8IEs,
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

		/*BugID_5137*/
		if (WILC_memcmp("DIRECT-", pstrHostIFconnectAttr->pu8ssid, 7)) {

			gu32FlushedInfoElemAsocSize = pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen;
			gu8FlushedInfoElemAsoc =  WILC_MALLOC(gu32FlushedInfoElemAsocSize);
			memcpy(gu8FlushedInfoElemAsoc, pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs,
			       gu32FlushedInfoElemAsocSize);
		}
	}
	strWIDList[u32WidsCount].u16WIDid = (u16)WID_11I_MODE;
	strWIDList[u32WidsCount].enuWIDtype = WID_CHAR;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(char);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&(pstrWFIDrv->strWILC_UsrConnReq.u8security));
	u32WidsCount++;

	/*BugID_5137*/
	if (WILC_memcmp("DIRECT-", pstrHostIFconnectAttr->pu8ssid, 7))
		gu8Flushed11iMode = pstrWFIDrv->strWILC_UsrConnReq.u8security;

	PRINT_INFO(HOSTINF_DBG, "Encrypt Mode = %x\n", pstrWFIDrv->strWILC_UsrConnReq.u8security);


	strWIDList[u32WidsCount].u16WIDid = (u16)WID_AUTH_TYPE;
	strWIDList[u32WidsCount].enuWIDtype = WID_CHAR;
	strWIDList[u32WidsCount].s32ValueSize = sizeof(char);
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)(&pstrWFIDrv->strWILC_UsrConnReq.tenuAuth_type);
	u32WidsCount++;

	/*BugID_5137*/
	if (WILC_memcmp("DIRECT-", pstrHostIFconnectAttr->pu8ssid, 7))
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


#ifndef WILC_PARSE_SCAN_IN_HOST
	strWIDList[u32WidsCount].u16WIDid = (u16)WID_JOIN_REQ_EXTENDED;
	strWIDList[u32WidsCount].enuWIDtype = WID_STR;
	strWIDList[u32WidsCount].s32ValueSize = MAX_SSID_LEN + 7;
	strWIDList[u32WidsCount].ps8WidVal = WILC_MALLOC(strWIDList[u32WidsCount].s32ValueSize);

	if (strWIDList[u32WidsCount].ps8WidVal == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_NO_MEM);
	}

	pu8CurrByte = strWIDList[u32WidsCount].ps8WidVal;

	if (pstrHostIFconnectAttr->pu8ssid != NULL) {
		WILC_memcpy(pu8CurrByte, pstrHostIFconnectAttr->pu8ssid, pstrHostIFconnectAttr->ssidLen);
		pu8CurrByte[pstrHostIFconnectAttr->ssidLen] = '\0';
	}
	pu8CurrByte += MAX_SSID_LEN;
	if ((pstrHostIFconnectAttr->u8channel >= 1) && (pstrHostIFconnectAttr->u8channel <= 14)) {
		*(pu8CurrByte++) = pstrHostIFconnectAttr->u8channel;
	} else {
		PRINT_ER("Channel out of range\n");
		*(pu8CurrByte++) = 0xFF;
	}
	if (pstrHostIFconnectAttr->pu8bssid != NULL) {
		WILC_memcpy(pu8CurrByte, pstrHostIFconnectAttr->pu8bssid, 6);
	}
	pu8CurrByte += 6;

	/* keep the buffer at the start of the allocated pointer to use it with the free*/
	pu8CurrByte = strWIDList[u32WidsCount].ps8WidVal;

	#else

	strWIDList[u32WidsCount].u16WIDid = (u16)WID_JOIN_REQ_EXTENDED;
	strWIDList[u32WidsCount].enuWIDtype = WID_STR;

	/*Sending NoA attributes during connection*/
	strWIDList[u32WidsCount].s32ValueSize = 112; /* 79; */
	strWIDList[u32WidsCount].ps8WidVal = WILC_MALLOC(strWIDList[u32WidsCount].s32ValueSize);

	/*BugID_5137*/
	if (WILC_memcmp("DIRECT-", pstrHostIFconnectAttr->pu8ssid, 7)) {
		gu32FlushedJoinReqSize = strWIDList[u32WidsCount].s32ValueSize;
		gu8FlushedJoinReq = WILC_MALLOC(gu32FlushedJoinReqSize);
	}
	if (strWIDList[u32WidsCount].ps8WidVal == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_NO_MEM);
	}

	pu8CurrByte = strWIDList[u32WidsCount].ps8WidVal;


	if (pstrHostIFconnectAttr->pu8ssid != NULL) {
		WILC_memcpy(pu8CurrByte, pstrHostIFconnectAttr->pu8ssid, pstrHostIFconnectAttr->ssidLen);
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
	if (pstrHostIFconnectAttr->pu8bssid != NULL) {
		WILC_memcpy(pu8CurrByte, pstrHostIFconnectAttr->pu8bssid, 6);
	}
	pu8CurrByte += 6;

	/* bssid*/
	if (pstrHostIFconnectAttr->pu8bssid != NULL) {
		WILC_memcpy(pu8CurrByte, pstrHostIFconnectAttr->pu8bssid, 6);
	}
	pu8CurrByte += 6;

	/* Beacon Period*/
	*(pu8CurrByte++)  = (ptstrJoinBssParam->beacon_period) & 0xFF;
	*(pu8CurrByte++)  = ((ptstrJoinBssParam->beacon_period) >> 8) & 0xFF;
	PRINT_D(HOSTINF_DBG, "* Beacon Period %d*\n", (*(pu8CurrByte - 2) | ((*(pu8CurrByte - 1)) << 8)));
	/* DTIM Period*/
	*(pu8CurrByte++)  =  ptstrJoinBssParam->dtim_period;
	PRINT_D(HOSTINF_DBG, "* DTIM Period %d*\n", (*(pu8CurrByte - 1)));
	/* Supported rates*/
	WILC_memcpy(pu8CurrByte, ptstrJoinBssParam->supp_rates, MAX_RATES_SUPPORTED + 1);
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
	WILC_memcpy(pu8CurrByte, ptstrJoinBssParam->rsn_pcip_policy, sizeof(ptstrJoinBssParam->rsn_pcip_policy));
	pu8CurrByte += sizeof(ptstrJoinBssParam->rsn_pcip_policy);

	/* rsn auth policy*/
	WILC_memcpy(pu8CurrByte, ptstrJoinBssParam->rsn_auth_policy, sizeof(ptstrJoinBssParam->rsn_auth_policy));
	pu8CurrByte += sizeof(ptstrJoinBssParam->rsn_auth_policy);

	/* rsn auth policy*/
	WILC_memcpy(pu8CurrByte, ptstrJoinBssParam->rsn_cap, sizeof(ptstrJoinBssParam->rsn_cap));
	pu8CurrByte += sizeof(ptstrJoinBssParam->rsn_cap);

	/*BugID_5137*/
	*(pu8CurrByte++) = REAL_JOIN_REQ;

		#ifdef WILC_P2P
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

		WILC_memcpy(pu8CurrByte, ptstrJoinBssParam->au8Duration, sizeof(ptstrJoinBssParam->au8Duration));

		pu8CurrByte += sizeof(ptstrJoinBssParam->au8Duration);

		WILC_memcpy(pu8CurrByte, ptstrJoinBssParam->au8Interval, sizeof(ptstrJoinBssParam->au8Interval));

		pu8CurrByte += sizeof(ptstrJoinBssParam->au8Interval);

		WILC_memcpy(pu8CurrByte, ptstrJoinBssParam->au8StartTime, sizeof(ptstrJoinBssParam->au8StartTime));

		pu8CurrByte += sizeof(ptstrJoinBssParam->au8StartTime);

	} else
		PRINT_D(HOSTINF_DBG, "NOA not present\n");
	#endif


	/* keep the buffer at the start of the allocated pointer to use it with the free*/
	pu8CurrByte = strWIDList[u32WidsCount].ps8WidVal;


	#endif /* #ifdef WILC_PARSE_SCAN_IN_HOST*/
	u32WidsCount++;

	#ifndef SIMULATION
	/* A temporary workaround to avoid handling the misleading MAC_DISCONNECTED raised from the
	 *   firmware at chip reset when processing the WIDs of the Connect Request.
	 *   (This workaround should be removed in the future when the Chip reset of the Connect WIDs is disabled) */
	/* ////////////////////// */
	gu32WidConnRstHack = 0;
	/* ////////////////////// */
	#endif

	/*BugID_5137*/
	if (WILC_memcmp("DIRECT-", pstrHostIFconnectAttr->pu8ssid, 7)) {
		memcpy(gu8FlushedJoinReq, pu8CurrByte, gu32FlushedJoinReqSize);
		gu8FlushedJoinReqDrvHandler = (u32)pstrWFIDrv;
	}

	PRINT_D(GENERIC_DBG, "send HOST_IF_WAITING_CONN_RESP\n");

	if (pstrHostIFconnectAttr->pu8bssid != NULL) {
		WILC_memcpy(u8ConnectedSSID, pstrHostIFconnectAttr->pu8bssid, ETH_ALEN);

		PRINT_D(GENERIC_DBG, "save Bssid = %x:%x:%x:%x:%x:%x\n", (pstrHostIFconnectAttr->pu8bssid[0]), (pstrHostIFconnectAttr->pu8bssid[1]), (pstrHostIFconnectAttr->pu8bssid[2]), (pstrHostIFconnectAttr->pu8bssid[3]), (pstrHostIFconnectAttr->pu8bssid[4]), (pstrHostIFconnectAttr->pu8bssid[5]));
		PRINT_D(GENERIC_DBG, "save bssid = %x:%x:%x:%x:%x:%x\n", (u8ConnectedSSID[0]), (u8ConnectedSSID[1]), (u8ConnectedSSID[2]), (u8ConnectedSSID[3]), (u8ConnectedSSID[4]), (u8ConnectedSSID[5]));
	}

	s32Error = SendConfigPkt(SET_CFG, strWIDList, u32WidsCount, false, (u32)pstrWFIDrv);
	if (s32Error) {
		PRINT_ER("Handle_Connect()] failed to send config packet\n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
	} else {
		PRINT_D(GENERIC_DBG, "set HOST_IF_WAITING_CONN_RESP\n");
		pstrWFIDrv->enuHostIFstate = HOST_IF_WAITING_CONN_RESP;
	}
	#endif

	WILC_CATCH(s32Error)
	{
		tstrConnectInfo strConnectInfo;

		WILC_TimerStop(&(pstrWFIDrv->hConnectTimer), NULL);

		PRINT_D(HOSTINF_DBG, "could not start connecting to the required network\n");

		WILC_memset(&strConnectInfo, 0, sizeof(tstrConnectInfo));

		if (pstrHostIFconnectAttr->pfConnectResult != NULL) {
			if (pstrHostIFconnectAttr->pu8bssid != NULL) {
				WILC_memcpy(strConnectInfo.au8bssid, pstrHostIFconnectAttr->pu8bssid, 6);
			}

			if (pstrHostIFconnectAttr->pu8IEs != NULL) {
				strConnectInfo.ReqIEsLen = pstrHostIFconnectAttr->IEsLen;
				strConnectInfo.pu8ReqIEs = (u8 *)WILC_MALLOC(pstrHostIFconnectAttr->IEsLen);
				WILC_memcpy(strConnectInfo.pu8ReqIEs,
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
				WILC_FREE(strConnectInfo.pu8ReqIEs);
				strConnectInfo.pu8ReqIEs = NULL;
			}

		} else {
			PRINT_ER("Connect callback function pointer is NULL \n");
		}
	}

	PRINT_D(HOSTINF_DBG, "Deallocating connection parameters\n");
	/* Deallocate pstrHostIFconnectAttr->pu8bssid which was prevoisuly allocated by the sending thread */
	if (pstrHostIFconnectAttr->pu8bssid != NULL) {
		WILC_FREE(pstrHostIFconnectAttr->pu8bssid);
		pstrHostIFconnectAttr->pu8bssid = NULL;
	}

	/* Deallocate pstrHostIFconnectAttr->pu8ssid which was prevoisuly allocated by the sending thread */
	if (pstrHostIFconnectAttr->pu8ssid != NULL) {
		WILC_FREE(pstrHostIFconnectAttr->pu8ssid);
		pstrHostIFconnectAttr->pu8ssid = NULL;
	}

	/* Deallocate pstrHostIFconnectAttr->pu8IEs which was prevoisuly allocated by the sending thread */
	if (pstrHostIFconnectAttr->pu8IEs != NULL) {
		WILC_FREE(pstrHostIFconnectAttr->pu8IEs);
		pstrHostIFconnectAttr->pu8IEs = NULL;
	}

	if (pu8CurrByte != NULL) {
		WILC_FREE(pu8CurrByte);
	}
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

static s32 Handle_FlushConnect(void *drvHandler)
{
	s32 s32Error = WILC_SUCCESS;
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


	#ifdef WILC_PARSE_SCAN_IN_HOST
	strWIDList[u32WidsCount].u16WIDid = (u16)WID_JOIN_REQ_EXTENDED;
	strWIDList[u32WidsCount].enuWIDtype = WID_STR;
	strWIDList[u32WidsCount].s32ValueSize = gu32FlushedJoinReqSize;
	strWIDList[u32WidsCount].ps8WidVal = (s8 *)gu8FlushedJoinReq;
	pu8CurrByte = strWIDList[u32WidsCount].ps8WidVal;

	pu8CurrByte += FLUSHED_BYTE_POS;
	*(pu8CurrByte) = FLUSHED_JOIN_REQ;

	u32WidsCount++;

	#endif

	s32Error = SendConfigPkt(SET_CFG, strWIDList, u32WidsCount, false, gu8FlushedJoinReqDrvHandler);
	if (s32Error) {
		PRINT_ER("Handle_Flush_Connect()] failed to send config packet\n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
	}

	WILC_CATCH(s32Error)
	{

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
static s32 Handle_ConnectTimeout(void *drvHandler)
{
	s32 s32Error = WILC_SUCCESS;
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


	WILC_memset(&strConnectInfo, 0, sizeof(tstrConnectInfo));


	/* First, we will notify the upper layer with the Connection failure {through the Connect Callback function},
	 *   then we will notify our firmware also with the Connection failure {through sending to it Cfg packet carrying
	 *   WID_DISCONNECT} */
	if (pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult != NULL)	{
		if (pstrWFIDrv->strWILC_UsrConnReq.pu8bssid != NULL) {
			WILC_memcpy(strConnectInfo.au8bssid,
				    pstrWFIDrv->strWILC_UsrConnReq.pu8bssid, 6);
		}

		if (pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) {
			strConnectInfo.ReqIEsLen = pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen;
			strConnectInfo.pu8ReqIEs = (u8 *)WILC_MALLOC(pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen);
			WILC_memcpy(strConnectInfo.pu8ReqIEs,
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
			WILC_FREE(strConnectInfo.pu8ReqIEs);
			strConnectInfo.pu8ReqIEs = NULL;
		}
	} else {
		PRINT_ER("Connect callback function pointer is NULL \n");
	}

	/* Here we will notify our firmware also with the Connection failure {through sending to it Cfg packet carrying
	 *   WID_DISCONNECT} */
	strWID.u16WIDid = (u16)WID_DISCONNECT;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = (s8 *)&u16DummyReasonCode;
	strWID.s32ValueSize = sizeof(char);

	PRINT_D(HOSTINF_DBG, "Sending disconnect request\n");

	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, false, (u32)pstrWFIDrv);
	if (s32Error) {
		PRINT_ER("Failed to send dissconect config packet\n");
	}

	/* Deallocation of the Saved Connect Request in the global Handle */
	pstrWFIDrv->strWILC_UsrConnReq.ssidLen = 0;
	if (pstrWFIDrv->strWILC_UsrConnReq.pu8ssid != NULL) {
		WILC_FREE(pstrWFIDrv->strWILC_UsrConnReq.pu8ssid);
		pstrWFIDrv->strWILC_UsrConnReq.pu8ssid = NULL;
	}

	if (pstrWFIDrv->strWILC_UsrConnReq.pu8bssid != NULL) {
		WILC_FREE(pstrWFIDrv->strWILC_UsrConnReq.pu8bssid);
		pstrWFIDrv->strWILC_UsrConnReq.pu8bssid = NULL;
	}

	pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen = 0;
	if (pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) {
		WILC_FREE(pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs);
		pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs = NULL;
	}

	WILC_memset(u8ConnectedSSID, 0, ETH_ALEN);
	/*BugID_5213*/
	/*Freeing flushed join request params on connect timeout*/
	if (gu8FlushedJoinReq != NULL && gu8FlushedJoinReqDrvHandler == (u32)drvHandler) {
		WILC_FREE(gu8FlushedJoinReq);
		gu8FlushedJoinReq = NULL;
	}
	if (gu8FlushedInfoElemAsoc != NULL && gu8FlushedJoinReqDrvHandler == (u32)drvHandler) {
		WILC_FREE(gu8FlushedInfoElemAsoc);
		gu8FlushedInfoElemAsoc = NULL;
	}

	return s32Error;
}

/**
 *  @brief Handle_RcvdNtwrkInfo
 *  @details       Handling received network information
 *  @param[in]    tstrRcvdNetworkInfo* pstrRcvdNetworkInfo
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_RcvdNtwrkInfo(void *drvHandler, tstrRcvdNetworkInfo *pstrRcvdNetworkInfo)
{
	u32 i;
	bool bNewNtwrkFound;



	s32 s32Error = WILC_SUCCESS;
	tstrNetworkInfo *pstrNetworkInfo = NULL;
	void *pJoinParams = NULL;

	tstrWILC_WFIDrv *pstrWFIDrv  = (tstrWILC_WFIDrv *)drvHandler;



	bNewNtwrkFound = true;
	PRINT_INFO(HOSTINF_DBG, "Handling received network info\n");

	/*if there is a an ongoing scan request*/
	if (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult) {
		PRINT_D(HOSTINF_DBG, "State: Scanning, parsing network information received\n");
		ParseNetworkInfo(pstrRcvdNetworkInfo->pu8Buffer, &pstrNetworkInfo);
		if ((pstrNetworkInfo == NULL)
		    || (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult == NULL)) {
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
		}

		/* check whether this network is discovered before */
		for (i = 0; i < pstrWFIDrv->strWILC_UsrScanReq.u32RcvdChCount; i++) {

			if ((pstrWFIDrv->strWILC_UsrScanReq.astrFoundNetworkInfo[i].au8bssid != NULL) &&
			    (pstrNetworkInfo->au8bssid != NULL)) {
				if (WILC_memcmp(pstrWFIDrv->strWILC_UsrScanReq.astrFoundNetworkInfo[i].au8bssid,
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
					WILC_memcpy(pstrWFIDrv->strWILC_UsrScanReq.astrFoundNetworkInfo[pstrWFIDrv->strWILC_UsrScanReq.u32RcvdChCount].au8bssid,
						    pstrNetworkInfo->au8bssid, 6);

					pstrWFIDrv->strWILC_UsrScanReq.u32RcvdChCount++;

					pstrNetworkInfo->bNewNetwork = true;
					/*Bug4218: Parsing Join Param*/
					/* add new BSS to JoinBssTable */
				#ifdef WILC_PARSE_SCAN_IN_HOST
					pJoinParams = host_int_ParseJoinBssParam(pstrNetworkInfo);
				#endif /*WILC_PARSE_SCAN_IN_HOST*/

					pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult(SCAN_EVENT_NETWORK_FOUND, pstrNetworkInfo,
											pstrWFIDrv->strWILC_UsrScanReq.u32UserScanPvoid,
											pJoinParams);


				}
			} else {
				PRINT_WRN(HOSTINF_DBG, "Discovered networks exceeded max. limit \n");
			}
		} else {
			pstrNetworkInfo->bNewNetwork = false;
			/* just call the User CallBack function to send the same discovered network with its updated RSSI */
			pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult(SCAN_EVENT_NETWORK_FOUND, pstrNetworkInfo,
									pstrWFIDrv->strWILC_UsrScanReq.u32UserScanPvoid, NULL);
		}
	}


	WILC_CATCH(s32Error)
	{

	}

done:
	/* Deallocate pstrRcvdNetworkInfo->pu8Buffer which was prevoisuly allocated by the sending thread */
	if (pstrRcvdNetworkInfo->pu8Buffer != NULL) {
		WILC_FREE(pstrRcvdNetworkInfo->pu8Buffer);
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
 *  @param[in]    tstrRcvdGnrlAsyncInfo* pstrRcvdGnrlAsyncInfo
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static s32 Handle_RcvdGnrlAsyncInfo(void *drvHandler, tstrRcvdGnrlAsyncInfo *pstrRcvdGnrlAsyncInfo)
{
	/* TODO: mostafa: till now, this function just handles only the received mac status msg, */
	/*				 which carries only 1 WID which have WID ID = WID_STATUS */
	s32 s32Error = WILC_SUCCESS;
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
	s32 s32Err = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *) drvHandler;
	if (pstrWFIDrv == NULL)	{
		PRINT_ER("Driver handler is NULL\n");
	}
	PRINT_D(GENERIC_DBG, "Current State = %d,Received state = %d\n", pstrWFIDrv->enuHostIFstate,
		pstrRcvdGnrlAsyncInfo->pu8Buffer[7]);

	if ((pstrWFIDrv->enuHostIFstate == HOST_IF_WAITING_CONN_RESP) ||
	    (pstrWFIDrv->enuHostIFstate == HOST_IF_CONNECTED) ||
	    pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult) {
		if ((pstrRcvdGnrlAsyncInfo->pu8Buffer == NULL) ||
		    (pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult == NULL)) {
			WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
		}

		u8MsgType = pstrRcvdGnrlAsyncInfo->pu8Buffer[0];

		/* Check whether the received message type is 'I' */
		if ('I' != u8MsgType) {
			PRINT_ER("Received Message format incorrect.\n");
			WILC_ERRORREPORT(s32Error, WILC_FAIL);
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

			WILC_memset(&strConnectInfo, 0, sizeof(tstrConnectInfo));

			if (u8MacStatus == MAC_CONNECTED) {
				WILC_memset(gapu8RcvdAssocResp, 0, MAX_ASSOC_RESP_FRAME_SIZE);

				host_int_get_assoc_res_info((WILC_WFIDrvHandle)pstrWFIDrv,
							    gapu8RcvdAssocResp,
							    MAX_ASSOC_RESP_FRAME_SIZE,
							    &u32RcvdAssocRespInfoLen);

				PRINT_INFO(HOSTINF_DBG, "Received association response with length = %d\n", u32RcvdAssocRespInfoLen);

				if (u32RcvdAssocRespInfoLen != 0) {

					PRINT_D(HOSTINF_DBG, "Parsing association response\n");
					s32Err = ParseAssocRespInfo(gapu8RcvdAssocResp, u32RcvdAssocRespInfoLen,
								    &pstrConnectRespInfo);
					if (s32Err) {
						PRINT_ER("ParseAssocRespInfo() returned error %d \n", s32Err);
					} else {
						/* use the necessary parsed Info from the Received Association Response */
						strConnectInfo.u16ConnectStatus = pstrConnectRespInfo->u16ConnectStatus;

						if (strConnectInfo.u16ConnectStatus == SUCCESSFUL_STATUSCODE) {
							PRINT_INFO(HOSTINF_DBG, "Association response received : Successful connection status\n");
							if (pstrConnectRespInfo->pu8RespIEs != NULL) {
								strConnectInfo.u16RespIEsLen = pstrConnectRespInfo->u16RespIEsLen;


								strConnectInfo.pu8RespIEs = (u8 *)WILC_MALLOC(pstrConnectRespInfo->u16RespIEsLen);
								WILC_memcpy(strConnectInfo.pu8RespIEs, pstrConnectRespInfo->pu8RespIEs,
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
				PRINT_ER("Received MAC status is MAC_CONNECTED while the received status code in Asoc Resp is not SUCCESSFUL_STATUSCODE \n");
				WILC_memset(u8ConnectedSSID, 0, ETH_ALEN);

			} else if (u8MacStatus == MAC_DISCONNECTED)    {
				PRINT_ER("Received MAC status is MAC_DISCONNECTED\n");
				WILC_memset(u8ConnectedSSID, 0, ETH_ALEN);
			}

			/* TODO: mostafa: correct BSSID should be retrieved from actual BSSID received from AP */
			/*               through a structure of type tstrConnectRespInfo */
			if (pstrWFIDrv->strWILC_UsrConnReq.pu8bssid != NULL) {
				PRINT_D(HOSTINF_DBG, "Retrieving actual BSSID from AP\n");
				WILC_memcpy(strConnectInfo.au8bssid, pstrWFIDrv->strWILC_UsrConnReq.pu8bssid, 6);

				if ((u8MacStatus == MAC_CONNECTED) &&
				    (strConnectInfo.u16ConnectStatus == SUCCESSFUL_STATUSCODE))	{
					WILC_memcpy(pstrWFIDrv->au8AssociatedBSSID,
						    pstrWFIDrv->strWILC_UsrConnReq.pu8bssid, ETH_ALEN);
				}
			}


			if (pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) {
				strConnectInfo.ReqIEsLen = pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen;
				strConnectInfo.pu8ReqIEs = (u8 *)WILC_MALLOC(pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen);
				WILC_memcpy(strConnectInfo.pu8ReqIEs,
					    pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs,
					    pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen);
			}


			WILC_TimerStop(&(pstrWFIDrv->hConnectTimer), NULL);
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
				#ifdef DISABLE_PWRSAVE_AND_SCAN_DURING_IP

				host_int_set_power_mgmt((WILC_WFIDrvHandle)pstrWFIDrv, 0, 0);
				#endif

				PRINT_D(HOSTINF_DBG, "MAC status : CONNECTED and Connect Status : Successful\n");
				pstrWFIDrv->enuHostIFstate = HOST_IF_CONNECTED;

				#ifdef DISABLE_PWRSAVE_AND_SCAN_DURING_IP
				PRINT_D(GENERIC_DBG, "Obtaining an IP, Disable Scan\n");
				g_obtainingIP = true;
				WILC_TimerStart(&hDuringIpTimer, 10000, NULL, NULL);
				#endif

				#ifdef WILC_PARSE_SCAN_IN_HOST
				/* open a BA session if possible */
				/* if(pstrWFIDrv->strWILC_UsrConnReq.IsHTCapable) */

				#endif

				/* host_int_addBASession(pstrWFIDrv->strWILC_UsrConnReq.pu8bssid,0, */
				/* BA_SESSION_DEFAULT_BUFFER_SIZE,BA_SESSION_DEFAULT_TIMEOUT); */
			} else {
				PRINT_D(HOSTINF_DBG, "MAC status : %d and Connect Status : %d\n", u8MacStatus, strConnectInfo.u16ConnectStatus);
				pstrWFIDrv->enuHostIFstate = HOST_IF_IDLE;
				gbScanWhileConnected = false;
			}

			/* Deallocation */
			if (strConnectInfo.pu8RespIEs != NULL) {
				WILC_FREE(strConnectInfo.pu8RespIEs);
				strConnectInfo.pu8RespIEs = NULL;
			}

			if (strConnectInfo.pu8ReqIEs != NULL) {
				WILC_FREE(strConnectInfo.pu8ReqIEs);
				strConnectInfo.pu8ReqIEs = NULL;
			}


			pstrWFIDrv->strWILC_UsrConnReq.ssidLen = 0;
			if (pstrWFIDrv->strWILC_UsrConnReq.pu8ssid != NULL) {
				WILC_FREE(pstrWFIDrv->strWILC_UsrConnReq.pu8ssid);
				pstrWFIDrv->strWILC_UsrConnReq.pu8ssid = NULL;
			}

			if (pstrWFIDrv->strWILC_UsrConnReq.pu8bssid != NULL) {
				WILC_FREE(pstrWFIDrv->strWILC_UsrConnReq.pu8bssid);
				pstrWFIDrv->strWILC_UsrConnReq.pu8bssid = NULL;
			}

			pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen = 0;
			if (pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) {
				WILC_FREE(pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs);
				pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs = NULL;
			}

		} else if ((u8MacStatus == MAC_DISCONNECTED) &&
			   (pstrWFIDrv->enuHostIFstate == HOST_IF_CONNECTED)) {
			/* Disassociation or Deauthentication frame has been received */
			PRINT_D(HOSTINF_DBG, "Received MAC_DISCONNECTED from the FW\n");

			WILC_memset(&strDisconnectNotifInfo, 0, sizeof(tstrDisconnectNotifInfo));

			if (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult) {
				PRINT_D(HOSTINF_DBG, "\n\n<< Abort the running OBSS Scan >> \n\n");
				WILC_TimerStop(&(pstrWFIDrv->hScanTimer), NULL);
				Handle_ScanDone((void *)pstrWFIDrv, SCAN_EVENT_ABORTED);
			}

			strDisconnectNotifInfo.u16reason = 0;
			strDisconnectNotifInfo.ie = NULL;
			strDisconnectNotifInfo.ie_len = 0;

			if (pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult != NULL)	{
				#ifdef DISABLE_PWRSAVE_AND_SCAN_DURING_IP

				g_obtainingIP = false;
				host_int_set_power_mgmt((WILC_WFIDrvHandle)pstrWFIDrv, 0, 0);
				#endif

				pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult(CONN_DISCONN_EVENT_DISCONN_NOTIF,
										   NULL,
										   0,
										   &strDisconnectNotifInfo,
										   pstrWFIDrv->strWILC_UsrConnReq.u32UserConnectPvoid);

			} else {
				PRINT_ER("Connect result callback function is NULL \n");
			}

			WILC_memset(pstrWFIDrv->au8AssociatedBSSID, 0, ETH_ALEN);


			/* Deallocation */

			/* if Information Elements were retrieved from the Received deauth/disassoc frame, then they
			 *  should be deallocated here */
			/*
			 * if(strDisconnectNotifInfo.ie != NULL)
			 * {
			 *      WILC_FREE(strDisconnectNotifInfo.ie);
			 *      strDisconnectNotifInfo.ie = NULL;
			 * }
			 */

			pstrWFIDrv->strWILC_UsrConnReq.ssidLen = 0;
			if (pstrWFIDrv->strWILC_UsrConnReq.pu8ssid != NULL) {
				WILC_FREE(pstrWFIDrv->strWILC_UsrConnReq.pu8ssid);
				pstrWFIDrv->strWILC_UsrConnReq.pu8ssid = NULL;
			}

			if (pstrWFIDrv->strWILC_UsrConnReq.pu8bssid != NULL) {
				WILC_FREE(pstrWFIDrv->strWILC_UsrConnReq.pu8bssid);
				pstrWFIDrv->strWILC_UsrConnReq.pu8bssid = NULL;
			}

			pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen = 0;
			if (pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) {
				WILC_FREE(pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs);
				pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs = NULL;
			}

			/*BugID_5213*/
			/*Freeing flushed join request params on receiving*/
			/*MAC_DISCONNECTED while connected*/
			if (gu8FlushedJoinReq != NULL && gu8FlushedJoinReqDrvHandler == (u32)drvHandler) {
				WILC_FREE(gu8FlushedJoinReq);
				gu8FlushedJoinReq = NULL;
			}
			if (gu8FlushedInfoElemAsoc != NULL && gu8FlushedJoinReqDrvHandler == (u32)drvHandler) {
				WILC_FREE(gu8FlushedInfoElemAsoc);
				gu8FlushedInfoElemAsoc = NULL;
			}

			pstrWFIDrv->enuHostIFstate = HOST_IF_IDLE;
			gbScanWhileConnected = false;

		} else if ((u8MacStatus == MAC_DISCONNECTED) &&
			   (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult != NULL)) {
			PRINT_D(HOSTINF_DBG, "Received MAC_DISCONNECTED from the FW while scanning\n");
			PRINT_D(HOSTINF_DBG, "\n\n<< Abort the running Scan >> \n\n");
			/*Abort the running scan*/
			WILC_TimerStop(&(pstrWFIDrv->hScanTimer), NULL);
			if (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult) {
				Handle_ScanDone((void *)pstrWFIDrv, SCAN_EVENT_ABORTED);

			}
		}

	}

	WILC_CATCH(s32Error)
	{

	}

	/* Deallocate pstrRcvdGnrlAsyncInfo->pu8Buffer which was prevoisuly allocated by the sending thread */
	if (pstrRcvdGnrlAsyncInfo->pu8Buffer != NULL) {
		WILC_FREE(pstrRcvdGnrlAsyncInfo->pu8Buffer);
		pstrRcvdGnrlAsyncInfo->pu8Buffer = NULL;
	}

	return s32Error;
}

/**
 *  @brief Handle_Key
 *  @details       Sending config packet to firmware to set key
 *  @param[in]    tstrHostIFkeyAttr* pstrHostIFkeyAttr
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
static int Handle_Key(void *drvHandler, tstrHostIFkeyAttr *pstrHostIFkeyAttr)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	#ifdef WILC_AP_EXTERNAL_MLME
	tstrWID strWIDList[5];
	#endif
	u8 i;
	u8 *pu8keybuf;
	s8 s8idxarray[1];
	s8 ret = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;


	switch (pstrHostIFkeyAttr->enuKeyType) {


	case WEP:

#ifdef WILC_AP_EXTERNAL_MLME
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


			pu8keybuf = (u8 *)WILC_MALLOC(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen);


			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send Key\n");
				return -1;
			}

			WILC_memcpy(pu8keybuf, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey,
				    pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen);


			WILC_FREE(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey);

			strWIDList[3].u16WIDid = (u16)WID_WEP_KEY_VALUE;
			strWIDList[3].enuWIDtype = WID_STR;
			strWIDList[3].s32ValueSize = pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen;
			strWIDList[3].ps8WidVal = (s8 *)pu8keybuf;


			s32Error = SendConfigPkt(SET_CFG, strWIDList, 4, true, (u32)pstrWFIDrv);
			WILC_FREE(pu8keybuf);


		}
#endif

		if (pstrHostIFkeyAttr->u8KeyAction & ADDKEY) {
			PRINT_D(HOSTINF_DBG, "Handling WEP key\n");
			pu8keybuf = (u8 *)WILC_MALLOC(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen + 2);
			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send Key\n");
				return -1;
			}
			pu8keybuf[0] = pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx;

			WILC_memcpy(pu8keybuf + 1, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen, 1);

			WILC_memcpy(pu8keybuf + 2, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey,
				    pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen);

			WILC_FREE(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey);

			strWID.u16WIDid	= (u16)WID_ADD_WEP_KEY;
			strWID.enuWIDtype	= WID_STR;
			strWID.ps8WidVal	= (s8 *)pu8keybuf;
			strWID.s32ValueSize = pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen + 2;

			s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
			WILC_FREE(pu8keybuf);
		} else if (pstrHostIFkeyAttr->u8KeyAction & REMOVEKEY)	  {

			PRINT_D(HOSTINF_DBG, "Removing key\n");
			strWID.u16WIDid	= (u16)WID_REMOVE_WEP_KEY;
			strWID.enuWIDtype	= WID_STR;

			s8idxarray[0] = (s8)pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx;
			strWID.ps8WidVal = s8idxarray;
			strWID.s32ValueSize = 1;

			s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
		} else {
			strWID.u16WIDid	= (u16)WID_KEY_ID;
			strWID.enuWIDtype	= WID_CHAR;
			strWID.ps8WidVal	= (s8 *)(&(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx));
			strWID.s32ValueSize = sizeof(char);

			PRINT_D(HOSTINF_DBG, "Setting default key index\n");

			s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
		}
		up(&(pstrWFIDrv->hSemTestKeyBlock));
		break;

	case WPARxGtk:
			#ifdef WILC_AP_EXTERNAL_MLME
		if (pstrHostIFkeyAttr->u8KeyAction & ADDKEY_AP)	{
			pu8keybuf = (u8 *)WILC_MALLOC(RX_MIC_KEY_MSG_LEN);
			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send RxGTK Key\n");
				ret = -1;
				goto _WPARxGtk_end_case_;
			}

			WILC_memset(pu8keybuf, 0, RX_MIC_KEY_MSG_LEN);


			/*|----------------------------------------------------------------------------|
			 * |Sta Address | Key RSC | KeyID | Key Length | Temporal Key	| Rx Michael Key |
			 * |------------|---------|-------|------------|---------------|----------------|
			 |	6 bytes	 | 8 byte  |1 byte |  1 byte	|   16 bytes	|	  8 bytes	 |*/



			if (pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8seq != NULL)
				WILC_memcpy(pu8keybuf + 6, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8seq, 8);


			WILC_memcpy(pu8keybuf + 14, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8keyidx, 1);

			WILC_memcpy(pu8keybuf + 15, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen, 1);

			WILC_memcpy(pu8keybuf + 16, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8key,
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

			s32Error = SendConfigPkt(SET_CFG, strWIDList, 2, true, (u32)pstrWFIDrv);

			WILC_FREE(pu8keybuf);

			/* ////////////////////////// */
			up(&(pstrWFIDrv->hSemTestKeyBlock));
			/* ///////////////////////// */
		}

			#endif
		if (pstrHostIFkeyAttr->u8KeyAction & ADDKEY) {
			PRINT_D(HOSTINF_DBG, "Handling group key(Rx) function\n");

			pu8keybuf = (u8 *)WILC_MALLOC(RX_MIC_KEY_MSG_LEN);
			if (pu8keybuf == NULL) {
				PRINT_ER("No buffer to send RxGTK Key\n");
				ret = -1;
				goto _WPARxGtk_end_case_;
			}

			WILC_memset(pu8keybuf, 0, RX_MIC_KEY_MSG_LEN);


			/*|----------------------------------------------------------------------------|
			 * |Sta Address | Key RSC | KeyID | Key Length | Temporal Key	| Rx Michael Key |
			 * |------------|---------|-------|------------|---------------|----------------|
			 |	6 bytes	 | 8 byte  |1 byte |  1 byte	|   16 bytes	|	  8 bytes	 |*/

			if (pstrWFIDrv->enuHostIFstate == HOST_IF_CONNECTED) {
				WILC_memcpy(pu8keybuf, pstrWFIDrv->au8AssociatedBSSID, ETH_ALEN);
			} else {
				PRINT_ER("Couldn't handle WPARxGtk while enuHostIFstate is not HOST_IF_CONNECTED \n");
			}

			WILC_memcpy(pu8keybuf + 6, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8seq, 8);

			WILC_memcpy(pu8keybuf + 14, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8keyidx, 1);

			WILC_memcpy(pu8keybuf + 15, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen, 1);
			WILC_memcpy(pu8keybuf + 16, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8key,
				    pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen);

			strWID.u16WIDid	= (u16)WID_ADD_RX_GTK;
			strWID.enuWIDtype	= WID_STR;
			strWID.ps8WidVal	= (s8 *)pu8keybuf;
			strWID.s32ValueSize = RX_MIC_KEY_MSG_LEN;

			s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);

			WILC_FREE(pu8keybuf);

			/* ////////////////////////// */
			up(&(pstrWFIDrv->hSemTestKeyBlock));
			/* ///////////////////////// */
		}
_WPARxGtk_end_case_:
		WILC_FREE(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8key);
		WILC_FREE(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8seq);
		if (ret == -1)
			return ret;

		break;

	case WPAPtk:
		#ifdef WILC_AP_EXTERNAL_MLME
		if (pstrHostIFkeyAttr->u8KeyAction & ADDKEY_AP)	{


			pu8keybuf = (u8 *)WILC_MALLOC(PTK_KEY_MSG_LEN + 1);



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

			WILC_memcpy(pu8keybuf, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8macaddr, 6);  /*1 bytes Key Length */

			WILC_memcpy(pu8keybuf + 6, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8keyidx, 1);
			WILC_memcpy(pu8keybuf + 7, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen, 1);
			/*16 byte TK*/
			WILC_memcpy(pu8keybuf + 8, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8key,
				    pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen);


			strWIDList[0].u16WIDid = (u16)WID_11I_MODE;
			strWIDList[0].enuWIDtype = WID_CHAR;
			strWIDList[0].s32ValueSize = sizeof(char);
			strWIDList[0].ps8WidVal = (s8 *)(&(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Ciphermode));

			strWIDList[1].u16WIDid	= (u16)WID_ADD_PTK;
			strWIDList[1].enuWIDtype	= WID_STR;
			strWIDList[1].ps8WidVal	= (s8 *)pu8keybuf;
			strWIDList[1].s32ValueSize = PTK_KEY_MSG_LEN + 1;

			s32Error = SendConfigPkt(SET_CFG, strWIDList, 2, true, (u32)pstrWFIDrv);
			WILC_FREE(pu8keybuf);

			/* ////////////////////////// */
			up(&(pstrWFIDrv->hSemTestKeyBlock));
			/* ///////////////////////// */
		}
		#endif
		if (pstrHostIFkeyAttr->u8KeyAction & ADDKEY) {


			pu8keybuf = (u8 *)WILC_MALLOC(PTK_KEY_MSG_LEN);



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

			WILC_memcpy(pu8keybuf, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8macaddr, 6);  /*1 bytes Key Length */

			WILC_memcpy(pu8keybuf + 6, &pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen, 1);
			/*16 byte TK*/
			WILC_memcpy(pu8keybuf + 7, pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8key,
				    pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen);


			strWID.u16WIDid	= (u16)WID_ADD_PTK;
			strWID.enuWIDtype	= WID_STR;
			strWID.ps8WidVal	= (s8 *)pu8keybuf;
			strWID.s32ValueSize = PTK_KEY_MSG_LEN;

			s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
			WILC_FREE(pu8keybuf);

			/* ////////////////////////// */
			up(&(pstrWFIDrv->hSemTestKeyBlock));
			/* ///////////////////////// */
		}

_WPAPtk_end_case_:
		WILC_FREE(pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFwpaAttr.pu8key);
		if (ret == -1)
			return ret;

		break;


	case PMKSA:

		PRINT_D(HOSTINF_DBG, "Handling PMKSA key\n");

		pu8keybuf = (u8 *)WILC_MALLOC((pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFpmkidAttr.numpmkid * PMKSA_KEY_LEN) + 1);
		if (pu8keybuf == NULL) {
			PRINT_ER("No buffer to send PMKSA Key\n");
			return -1;
		}

		pu8keybuf[0] = pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFpmkidAttr.numpmkid;

		for (i = 0; i < pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFpmkidAttr.numpmkid; i++) {

			WILC_memcpy(pu8keybuf + ((PMKSA_KEY_LEN * i) + 1), pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFpmkidAttr.pmkidlist[i].bssid, ETH_ALEN);
			WILC_memcpy(pu8keybuf + ((PMKSA_KEY_LEN * i) + ETH_ALEN + 1), pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFpmkidAttr.pmkidlist[i].pmkid, PMKID_LEN);
		}

		strWID.u16WIDid	= (u16)WID_PMKID_INFO;
		strWID.enuWIDtype = WID_STR;
		strWID.ps8WidVal = (s8 *)pu8keybuf;
		strWID.s32ValueSize = (pstrHostIFkeyAttr->uniHostIFkeyAttr.strHostIFpmkidAttr.numpmkid * PMKSA_KEY_LEN) + 1;

		s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);

		WILC_FREE(pu8keybuf);
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
static void Handle_Disconnect(void *drvHandler)
{
	tstrWID strWID;

	s32 s32Error = WILC_SUCCESS;
	u16 u16DummyReasonCode = 0;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;


	strWID.u16WIDid = (u16)WID_DISCONNECT;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = (s8 *)&u16DummyReasonCode;
	strWID.s32ValueSize = sizeof(char);



	PRINT_D(HOSTINF_DBG, "Sending disconnect request\n");

	#ifdef DISABLE_PWRSAVE_AND_SCAN_DURING_IP

	g_obtainingIP = false;
	host_int_set_power_mgmt((WILC_WFIDrvHandle)pstrWFIDrv, 0, 0);
	#endif

	WILC_memset(u8ConnectedSSID, 0, ETH_ALEN);

	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, false, (u32)pstrWFIDrv);

	if (s32Error) {
		PRINT_ER("Failed to send dissconect config packet\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	} else {
		tstrDisconnectNotifInfo strDisconnectNotifInfo;

		WILC_memset(&strDisconnectNotifInfo, 0, sizeof(tstrDisconnectNotifInfo));

		strDisconnectNotifInfo.u16reason = 0;
		strDisconnectNotifInfo.ie = NULL;
		strDisconnectNotifInfo.ie_len = 0;

		if (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult) {
			WILC_TimerStop(&(pstrWFIDrv->hScanTimer), NULL);
			pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult(SCAN_EVENT_ABORTED, NULL,
									pstrWFIDrv->strWILC_UsrScanReq.u32UserScanPvoid, NULL);

			pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult = NULL;
		}

		if (pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult != NULL)	{

			/*BugID_5193*/
			/*Stop connect timer, if connection in progress*/
			if (pstrWFIDrv->enuHostIFstate == HOST_IF_WAITING_CONN_RESP) {
				PRINT_D(HOSTINF_DBG, "Upper layer requested termination of connection\n");
				WILC_TimerStop(&(pstrWFIDrv->hConnectTimer), NULL);
			}

			pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult(CONN_DISCONN_EVENT_DISCONN_NOTIF, NULL,
									   0, &strDisconnectNotifInfo, pstrWFIDrv->strWILC_UsrConnReq.u32UserConnectPvoid);
		} else {
			PRINT_ER("strWILC_UsrConnReq.pfUserConnectResult = NULL \n");
		}

		gbScanWhileConnected = false;

		pstrWFIDrv->enuHostIFstate = HOST_IF_IDLE;

		WILC_memset(pstrWFIDrv->au8AssociatedBSSID, 0, ETH_ALEN);


		/* Deallocation */
		pstrWFIDrv->strWILC_UsrConnReq.ssidLen = 0;
		if (pstrWFIDrv->strWILC_UsrConnReq.pu8ssid != NULL) {
			WILC_FREE(pstrWFIDrv->strWILC_UsrConnReq.pu8ssid);
			pstrWFIDrv->strWILC_UsrConnReq.pu8ssid = NULL;
		}

		if (pstrWFIDrv->strWILC_UsrConnReq.pu8bssid != NULL) {
			WILC_FREE(pstrWFIDrv->strWILC_UsrConnReq.pu8bssid);
			pstrWFIDrv->strWILC_UsrConnReq.pu8bssid = NULL;
		}

		pstrWFIDrv->strWILC_UsrConnReq.ConnReqIEsLen = 0;
		if (pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs != NULL) {
			WILC_FREE(pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs);
			pstrWFIDrv->strWILC_UsrConnReq.pu8ConnReqIEs = NULL;
		}


		/*BugID_5137*/
		if (gu8FlushedJoinReq != NULL && gu8FlushedJoinReqDrvHandler == (u32)drvHandler) {
			WILC_FREE(gu8FlushedJoinReq);
			gu8FlushedJoinReq = NULL;
		}
		if (gu8FlushedInfoElemAsoc != NULL && gu8FlushedJoinReqDrvHandler == (u32)drvHandler) {
			WILC_FREE(gu8FlushedInfoElemAsoc);
			gu8FlushedInfoElemAsoc = NULL;
		}

	}

	WILC_CATCH(s32Error)
	{

	}

	/* ////////////////////////// */
	up(&(pstrWFIDrv->hSemTestDisconnectBlock));
	/* ///////////////////////// */

}


void resolve_disconnect_aberration(void *drvHandler)
{
	tstrWILC_WFIDrv *pstrWFIDrv;

	pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;
	if (pstrWFIDrv  == NULL)
		return;
	if ((pstrWFIDrv->enuHostIFstate == HOST_IF_WAITING_CONN_RESP) || (pstrWFIDrv->enuHostIFstate == HOST_IF_CONNECTING)) {
		PRINT_D(HOSTINF_DBG, "\n\n<< correcting Supplicant state machine >>\n\n");
		host_int_disconnect((WILC_WFIDrvHandle)pstrWFIDrv, 1);
	}
}
static s32 Switch_Log_Terminal(void *drvHandler)
{


	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	static char dummy = 9;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	strWID.u16WIDid = (u16)WID_LOGTerminal_Switch;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = &dummy;
	strWID.s32ValueSize = sizeof(char);

	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);


	if (s32Error) {
		PRINT_D(HOSTINF_DBG, "Failed to switch log terminal\n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
	} else {
		PRINT_INFO(HOSTINF_DBG, "MAC address set :: \n");


	}

	WILC_CATCH(s32Error)
	{

	}

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
static s32 Handle_GetChnl(void *drvHandler)
{

	s32 s32Error = WILC_SUCCESS;
	tstrWID	strWID;
	/* tstrWILC_WFIDrv * pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv; */
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;
	strWID.u16WIDid = (u16)WID_CURRENT_CHANNEL;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = (s8 *)&gu8Chnl;
	strWID.s32ValueSize = sizeof(char);

	PRINT_D(HOSTINF_DBG, "Getting channel value\n");

	s32Error = SendConfigPkt(GET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	/*get the value by searching the local copy*/
	if (s32Error) {
		PRINT_ER("Failed to get channel number\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}


	WILC_CATCH(s32Error)
	{

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
static void Handle_GetRssi(void *drvHandler)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	strWID.u16WIDid = (u16)WID_RSSI;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = &gs8Rssi;
	strWID.s32ValueSize = sizeof(char);

	/*Sending Cfg*/
	PRINT_D(HOSTINF_DBG, "Getting RSSI value\n");

	s32Error = SendConfigPkt(GET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	if (s32Error) {
		PRINT_ER("Failed to get RSSI value\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	WILC_CATCH(s32Error)
	{

	}
	up(&(pstrWFIDrv->hSemGetRSSI));


}


static void Handle_GetLinkspeed(void *drvHandler)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	gs8lnkspd = 0;

	strWID.u16WIDid = (u16)WID_LINKSPEED;
	strWID.enuWIDtype = WID_CHAR;
	strWID.ps8WidVal = &gs8lnkspd;
	strWID.s32ValueSize = sizeof(char);
	/*Sending Cfg*/
	PRINT_D(HOSTINF_DBG, "Getting LINKSPEED value\n");

	s32Error = SendConfigPkt(GET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	if (s32Error) {
		PRINT_ER("Failed to get LINKSPEED value\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	WILC_CATCH(s32Error)
	{

	}
	up(&(pstrWFIDrv->hSemGetLINKSPEED));


}

s32 Handle_GetStatistics(void *drvHandler, tstrStatistics *pstrStatistics)
{
	tstrWID strWIDList[5];
	uint32_t u32WidsCount = 0, s32Error = 0;

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

	s32Error = SendConfigPkt(GET_CFG, strWIDList, u32WidsCount, false, (u32)drvHandler);

	if (s32Error) {
		PRINT_ER("Failed to send scan paramters config packet\n");
		/* WILC_ERRORREPORT(s32Error, s32Error); */
	}
	up(&hWaitResponse);
	return 0;

}


#ifdef WILC_AP_EXTERNAL_MLME


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
static s32 Handle_Get_InActiveTime(void *drvHandler, tstrHostIfStaInactiveT *strHostIfStaInactiveT)
{

	s32 s32Error = WILC_SUCCESS;
	u8 *stamac;
	tstrWID	strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;


	strWID.u16WIDid = (u16)WID_SET_STA_MAC_INACTIVE_TIME;
	strWID.enuWIDtype = WID_STR;
	strWID.s32ValueSize = ETH_ALEN;
	strWID.ps8WidVal = (u8 *)WILC_MALLOC(strWID.s32ValueSize);


	stamac = strWID.ps8WidVal;
	WILC_memcpy(stamac, strHostIfStaInactiveT->mac, ETH_ALEN);


	PRINT_D(CFG80211_DBG, "SETING STA inactive time\n");


	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	/*get the value by searching the local copy*/
	if (s32Error) {
		PRINT_ER("Failed to SET incative time\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}


	strWID.u16WIDid = (u16)WID_GET_INACTIVE_TIME;
	strWID.enuWIDtype = WID_INT;
	strWID.ps8WidVal = (s8 *)&gu32InactiveTime;
	strWID.s32ValueSize = sizeof(u32);


	s32Error = SendConfigPkt(GET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	/*get the value by searching the local copy*/
	if (s32Error) {
		PRINT_ER("Failed to get incative time\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}


	PRINT_D(CFG80211_DBG, "Getting inactive time : %d\n", gu32InactiveTime);

	up(&(pstrWFIDrv->hSemInactiveTime));
	WILC_CATCH(s32Error)
	{

	}


	return s32Error;



}


/**
 *  @brief Handle_AddBeacon
 *  @details       Sending config packet to add beacon
 *  @param[in]    tstrHostIFSetBeacon* pstrSetBeaconParam
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static void Handle_AddBeacon(void *drvHandler, tstrHostIFSetBeacon *pstrSetBeaconParam)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	u8 *pu8CurrByte;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;
	PRINT_D(HOSTINF_DBG, "Adding BEACON\n");

	strWID.u16WIDid = (u16)WID_ADD_BEACON;
	strWID.enuWIDtype = WID_BIN;
	strWID.s32ValueSize = pstrSetBeaconParam->u32HeadLen + pstrSetBeaconParam->u32TailLen + 16;
	strWID.ps8WidVal = WILC_MALLOC(strWID.s32ValueSize);
	if (strWID.ps8WidVal == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_NO_MEM);
	}

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

	/* Bug 4599 : if tail length = 0 skip copying */
	if (pstrSetBeaconParam->pu8Tail > 0)
		memcpy(pu8CurrByte, pstrSetBeaconParam->pu8Tail, pstrSetBeaconParam->u32TailLen);
	pu8CurrByte += pstrSetBeaconParam->u32TailLen;



	/*Sending Cfg*/
	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, false, (u32)pstrWFIDrv);
	if (s32Error) {
		PRINT_ER("Failed to send add beacon config packet\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	WILC_CATCH(s32Error)
	{
	}
	WILC_FREE_IF_TRUE(strWID.ps8WidVal);
	WILC_FREE_IF_TRUE(pstrSetBeaconParam->pu8Head);
	WILC_FREE_IF_TRUE(pstrSetBeaconParam->pu8Tail);
}


/**
 *  @brief Handle_AddBeacon
 *  @details       Sending config packet to delete beacon
 *  @param[in]   tstrHostIFDelBeacon* pstrDelBeacon
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static void Handle_DelBeacon(void *drvHandler, tstrHostIFDelBeacon *pstrDelBeacon)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	u8 *pu8CurrByte;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;
	strWID.u16WIDid = (u16)WID_DEL_BEACON;
	strWID.enuWIDtype = WID_CHAR;
	strWID.s32ValueSize = sizeof(char);
	strWID.ps8WidVal = &gu8DelBcn;

	if (strWID.ps8WidVal == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_NO_MEM);
	}

	pu8CurrByte = strWID.ps8WidVal;

	PRINT_D(HOSTINF_DBG, "Deleting BEACON\n");
	/* TODO: build del beacon message*/

	/*Sending Cfg*/
	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, false, (u32)pstrWFIDrv);
	if (s32Error) {

		PRINT_ER("Failed to send delete beacon config packet\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	WILC_CATCH(s32Error)
	{
	}
}


/**
 *  @brief WILC_HostIf_PackStaParam
 *  @details       Handling packing of the station params in a buffer
 *  @param[in]   u8* pu8Buffer, tstrWILC_AddStaParam* pstrStationParam
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static u32 WILC_HostIf_PackStaParam(u8 *pu8Buffer, tstrWILC_AddStaParam *pstrStationParam)
{
	u8 *pu8CurrByte;

	pu8CurrByte = pu8Buffer;

	PRINT_D(HOSTINF_DBG, "Packing STA params\n");
	WILC_memcpy(pu8CurrByte, pstrStationParam->au8BSSID, ETH_ALEN);
	pu8CurrByte +=  ETH_ALEN;

	*pu8CurrByte++ = pstrStationParam->u16AssocID & 0xFF;
	*pu8CurrByte++ = (pstrStationParam->u16AssocID >> 8) & 0xFF;

	*pu8CurrByte++ = pstrStationParam->u8NumRates;
	if (pstrStationParam->u8NumRates > 0) {
		WILC_memcpy(pu8CurrByte, pstrStationParam->pu8Rates, pstrStationParam->u8NumRates);
	}
	pu8CurrByte += pstrStationParam->u8NumRates;

	*pu8CurrByte++ = pstrStationParam->bIsHTSupported;
	*pu8CurrByte++ = pstrStationParam->u16HTCapInfo & 0xFF;
	*pu8CurrByte++ = (pstrStationParam->u16HTCapInfo >> 8) & 0xFF;

	*pu8CurrByte++ = pstrStationParam->u8AmpduParams;
	WILC_memcpy(pu8CurrByte, pstrStationParam->au8SuppMCsSet, WILC_SUPP_MCS_SET_SIZE);
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
 *  @param[in]   tstrWILC_AddStaParam* pstrStationParam
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static void Handle_AddStation(void *drvHandler, tstrWILC_AddStaParam *pstrStationParam)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	u8 *pu8CurrByte;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;
	PRINT_D(HOSTINF_DBG, "Handling add station\n");
	strWID.u16WIDid = (u16)WID_ADD_STA;
	strWID.enuWIDtype = WID_BIN;
	strWID.s32ValueSize = WILC_ADD_STA_LENGTH + pstrStationParam->u8NumRates;

	strWID.ps8WidVal = WILC_MALLOC(strWID.s32ValueSize);
	if (strWID.ps8WidVal == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_NO_MEM);
	}

	pu8CurrByte = strWID.ps8WidVal;
	pu8CurrByte += WILC_HostIf_PackStaParam(pu8CurrByte, pstrStationParam);

	/*Sending Cfg*/
	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, false, (u32)pstrWFIDrv);
	if (s32Error != WILC_SUCCESS) {

		PRINT_ER("Failed to send add station config packet\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	WILC_CATCH(s32Error)
	{
	}
	WILC_FREE_IF_TRUE(pstrStationParam->pu8Rates);
	WILC_FREE_IF_TRUE(strWID.ps8WidVal);
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
static void Handle_DelAllSta(void *drvHandler, tstrHostIFDelAllSta *pstrDelAllStaParam)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	u8 *pu8CurrByte;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;
	u8 i;
	u8 au8Zero_Buff[6] = {0};
	strWID.u16WIDid = (u16)WID_DEL_ALL_STA;
	strWID.enuWIDtype = WID_STR;
	strWID.s32ValueSize = (pstrDelAllStaParam->u8Num_AssocSta * ETH_ALEN) + 1;

	PRINT_D(HOSTINF_DBG, "Handling delete station \n");

	strWID.ps8WidVal = WILC_MALLOC((pstrDelAllStaParam->u8Num_AssocSta * ETH_ALEN) + 1);
	if (strWID.ps8WidVal == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_NO_MEM);
	}

	pu8CurrByte = strWID.ps8WidVal;

	*(pu8CurrByte++) = pstrDelAllStaParam->u8Num_AssocSta;

	for (i = 0; i < MAX_NUM_STA; i++) {
		if (memcmp(pstrDelAllStaParam->au8Sta_DelAllSta[i], au8Zero_Buff, ETH_ALEN))
			WILC_memcpy(pu8CurrByte, pstrDelAllStaParam->au8Sta_DelAllSta[i], ETH_ALEN);
		else
			continue;

		pu8CurrByte += ETH_ALEN;
	}

	/*Sending Cfg*/
	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	if (s32Error) {

		PRINT_ER("Failed to send add station config packe\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	WILC_CATCH(s32Error)
	{
	}
	WILC_FREE_IF_TRUE(strWID.ps8WidVal);

	up(&hWaitResponse);
}


/**
 *  @brief Handle_DelStation
 *  @details        Sending config packet to delete station
 *  @param[in]   tstrHostIFDelSta* pstrDelStaParam
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static void Handle_DelStation(void *drvHandler, tstrHostIFDelSta *pstrDelStaParam)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	u8 *pu8CurrByte;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	strWID.u16WIDid = (u16)WID_REMOVE_STA;
	strWID.enuWIDtype = WID_BIN;
	strWID.s32ValueSize = ETH_ALEN;

	PRINT_D(HOSTINF_DBG, "Handling delete station \n");

	strWID.ps8WidVal = WILC_MALLOC(strWID.s32ValueSize);
	if (strWID.ps8WidVal == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_NO_MEM);
	}

	pu8CurrByte = strWID.ps8WidVal;

	WILC_memcpy(pu8CurrByte, pstrDelStaParam->au8MacAddr, ETH_ALEN);

	/*Sending Cfg*/
	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, false, (u32)pstrWFIDrv);
	if (s32Error) {

		PRINT_ER("Failed to send add station config packe\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	WILC_CATCH(s32Error)
	{
	}
	WILC_FREE_IF_TRUE(strWID.ps8WidVal);
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
static void Handle_EditStation(void *drvHandler, tstrWILC_AddStaParam *pstrStationParam)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	u8 *pu8CurrByte;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	strWID.u16WIDid = (u16)WID_EDIT_STA;
	strWID.enuWIDtype = WID_BIN;
	strWID.s32ValueSize = WILC_ADD_STA_LENGTH + pstrStationParam->u8NumRates;

	PRINT_D(HOSTINF_DBG, "Handling edit station\n");
	strWID.ps8WidVal = WILC_MALLOC(strWID.s32ValueSize);
	if (strWID.ps8WidVal == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_NO_MEM);
	}

	pu8CurrByte = strWID.ps8WidVal;
	pu8CurrByte += WILC_HostIf_PackStaParam(pu8CurrByte, pstrStationParam);

	/*Sending Cfg*/
	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, false, (u32)pstrWFIDrv);
	if (s32Error) {

		PRINT_ER("Failed to send edit station config packet\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	WILC_CATCH(s32Error)
	{
	}
	WILC_FREE_IF_TRUE(pstrStationParam->pu8Rates);
	WILC_FREE_IF_TRUE(strWID.ps8WidVal);
}
#endif /*WILC_AP_EXTERNAL_MLME*/

#ifdef WILC_P2P
/**
 *  @brief Handle_RemainOnChan
 *  @details        Sending config packet to edit station
 *  @param[in]   tstrWILC_AddStaParam* pstrStationParam
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static int Handle_RemainOnChan(void *drvHandler, tstrHostIfRemainOnChan *pstrHostIfRemainOnChan)
{
	s32 s32Error = WILC_SUCCESS;
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
		WILC_ERRORREPORT(s32Error, WILC_BUSY);
	}
	if (pstrWFIDrv->enuHostIFstate == HOST_IF_WAITING_CONN_RESP) {
		PRINT_INFO(GENERIC_DBG, "Required to remain on chan while connecting return\n");
		WILC_ERRORREPORT(s32Error, WILC_BUSY);
	}

	#ifdef DISABLE_PWRSAVE_AND_SCAN_DURING_IP
	if (g_obtainingIP || connecting) {
		PRINT_D(GENERIC_DBG, "[handle_scan]: Don't do obss scan until IP adresss is obtained\n");
		WILC_ERRORREPORT(s32Error, WILC_BUSY);
	}
	#endif

	PRINT_D(HOSTINF_DBG, "Setting channel :%d\n", pstrHostIfRemainOnChan->u16Channel);

	u8remain_on_chan_flag = true;
	strWID.u16WIDid	= (u16)WID_REMAIN_ON_CHAN;
	strWID.enuWIDtype	= WID_STR;
	strWID.s32ValueSize = 2;
	strWID.ps8WidVal = (s8 *)WILC_MALLOC(strWID.s32ValueSize);

	if (strWID.ps8WidVal == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_NO_MEM);
	}

	strWID.ps8WidVal[0] = u8remain_on_chan_flag;
	strWID.ps8WidVal[1] = (s8)pstrHostIfRemainOnChan->u16Channel;

	/*Sending Cfg*/
	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	if (s32Error != WILC_SUCCESS) {
		PRINT_ER("Failed to set remain on channel\n");
	}

	WILC_CATCH(-1)
	{
		P2P_LISTEN_STATE = 1;
		WILC_TimerStart(&(pstrWFIDrv->hRemainOnChannel), pstrHostIfRemainOnChan->u32duration, (void *)pstrWFIDrv, NULL);

		/*Calling CFG ready_on_channel*/
		if (pstrWFIDrv->strHostIfRemainOnChan.pRemainOnChanReady) {
			pstrWFIDrv->strHostIfRemainOnChan.pRemainOnChanReady(pstrWFIDrv->strHostIfRemainOnChan.pVoid);
		}

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
static int Handle_RegisterFrame(void *drvHandler, tstrHostIfRegisterFrame *pstrHostIfRegisterFrame)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	u8 *pu8CurrByte;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	PRINT_D(HOSTINF_DBG, "Handling frame register Flag : %d FrameType: %d\n", pstrHostIfRegisterFrame->bReg, pstrHostIfRegisterFrame->u16FrameType);

	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_REGISTER_FRAME;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = WILC_MALLOC(sizeof(u16) + 2);
	if (strWID.ps8WidVal == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_NO_MEM);
	}

	pu8CurrByte = strWID.ps8WidVal;

	*pu8CurrByte++ = pstrHostIfRegisterFrame->bReg;
	*pu8CurrByte++ = pstrHostIfRegisterFrame->u8Regid;
	WILC_memcpy(pu8CurrByte, &(pstrHostIfRegisterFrame->u16FrameType), sizeof(u16));


	strWID.s32ValueSize = sizeof(u16) + 2;


	/*Sending Cfg*/
	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	if (s32Error) {
		PRINT_ER("Failed to frame register config packet\n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
	}


	WILC_CATCH(s32Error)
	{
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
static u32 Handle_ListenStateExpired(void *drvHandler, tstrHostIfRemainOnChan *pstrHostIfRemainOnChan)
{
	u8 u8remain_on_chan_flag;
	tstrWID strWID;
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *) drvHandler;

	PRINT_D(HOSTINF_DBG, "CANCEL REMAIN ON CHAN\n");

	/*BugID_5477*/
	/*Make sure we are already in listen state*/
	/*This is to handle duplicate expiry messages (listen timer fired and supplicant called cancel_remain_on_channel())*/
	if (P2P_LISTEN_STATE) {
		u8remain_on_chan_flag = false;
		strWID.u16WIDid	= (u16)WID_REMAIN_ON_CHAN;
		strWID.enuWIDtype	= WID_STR;
		strWID.s32ValueSize = 2;
		strWID.ps8WidVal = WILC_MALLOC(strWID.s32ValueSize);

		if (strWID.ps8WidVal == NULL) {
			PRINT_ER("Failed to allocate memory\n");
		}

		strWID.ps8WidVal[0] = u8remain_on_chan_flag;
		strWID.ps8WidVal[1] = FALSE_FRMWR_CHANNEL;

		/*Sending Cfg*/
		s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
		if (s32Error != WILC_SUCCESS) {
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
		s32Error = WILC_FAIL;
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
static void ListenTimerCB(void *pvArg)
{
	s32 s32Error = WILC_SUCCESS;
	tstrHostIFmsg strHostIFmsg;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)pvArg;
	/*Stopping remain-on-channel timer*/
	WILC_TimerStop(&(pstrWFIDrv->hRemainOnChannel), NULL);

	/* prepare the Timer Callback message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));
	strHostIFmsg.u16MsgId = HOST_IF_MSG_LISTEN_TIMER_FIRED;
	strHostIFmsg.drvHandler = pstrWFIDrv;
	strHostIFmsg.uniHostIFmsgBody.strHostIfRemainOnChan.u32ListenSessionID = pstrWFIDrv->strHostIfRemainOnChan.u32ListenSessionID;

	/* send the message */
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{

	}
}
#endif


/**
 *  @brief Handle_EditStation
 *  @details        Sending config packet to edit station
 *  @param[in]   tstrWILC_AddStaParam* pstrStationParam
 *  @return         NONE
 *  @author
 *  @date
 *  @version	1.0
 */
static void Handle_PowerManagement(void *drvHandler, tstrHostIfPowerMgmtParam *strPowerMgmtParam)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	s8 s8PowerMode;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;
	strWID.u16WIDid = (u16)WID_POWER_MANAGEMENT;

	if (strPowerMgmtParam->bIsEnabled == true)	{
		s8PowerMode = MIN_FAST_PS;
	} else {
		s8PowerMode = NO_POWERSAVE;
	}
	PRINT_D(HOSTINF_DBG, "Handling power mgmt to %d\n", s8PowerMode);
	strWID.ps8WidVal = &s8PowerMode;
	strWID.s32ValueSize = sizeof(char);

	PRINT_D(HOSTINF_DBG, "Handling Power Management\n");

	/*Sending Cfg*/
	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	if (s32Error) {
		PRINT_ER("Failed to send power management config packet\n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
	}

	WILC_CATCH(s32Error)
	{

	}
}

/**
 *  @brief Handle_SetMulticastFilter
 *  @details        Set Multicast filter in firmware
 *  @param[in]   tstrHostIFSetMulti* strHostIfSetMulti
 *  @return         NONE
 *  @author		asobhy
 *  @date
 *  @version	1.0
 */
static void Handle_SetMulticastFilter(void *drvHandler, tstrHostIFSetMulti *strHostIfSetMulti)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	u8 *pu8CurrByte;

	PRINT_D(HOSTINF_DBG, "Setup Multicast Filter\n");

	strWID.u16WIDid = (u16)WID_SETUP_MULTICAST_FILTER;
	strWID.enuWIDtype = WID_BIN;
	strWID.s32ValueSize = sizeof(tstrHostIFSetMulti) + ((strHostIfSetMulti->u32count) * ETH_ALEN);
	strWID.ps8WidVal = WILC_MALLOC(strWID.s32ValueSize);
	if (strWID.ps8WidVal == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_NO_MEM);
	}

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
	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, false, (u32)drvHandler);
	if (s32Error) {
		PRINT_ER("Failed to send setup multicast config packet\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	WILC_CATCH(s32Error)
	{
	}
	WILC_FREE_IF_TRUE(strWID.ps8WidVal);

}


/*BugID_5222*/
/**
 *  @brief                      Handle_AddBASession
 *  @details            Add block ack session
 *  @param[in]          tstrHostIFSetMulti* strHostIfSetMulti
 *  @return             NONE
 *  @author		Amr Abdel-Moghny
 *  @date			Feb. 2014
 *  @version		9.0
 */
static s32 Handle_AddBASession(void *drvHandler, tstrHostIfBASessionInfo *strHostIfBASessionInfo)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	int AddbaTimeout = 100;
	char *ptr = NULL;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	PRINT_D(HOSTINF_DBG, "Opening Block Ack session with\nBSSID = %.2x:%.2x:%.2x \nTID=%d \nBufferSize == %d \nSessionTimeOut = %d\n",
		strHostIfBASessionInfo->au8Bssid[0],
		strHostIfBASessionInfo->au8Bssid[1],
		strHostIfBASessionInfo->au8Bssid[2],
		strHostIfBASessionInfo->u16BufferSize,
		strHostIfBASessionInfo->u16SessionTimeout,
		strHostIfBASessionInfo->u8Ted);

	strWID.u16WIDid = (u16)WID_11E_P_ACTION_REQ;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = (u8 *)WILC_MALLOC(BLOCK_ACK_REQ_SIZE);
	strWID.s32ValueSize = BLOCK_ACK_REQ_SIZE;
	ptr = strWID.ps8WidVal;
	/* *ptr++ = 0x14; */
	*ptr++ = 0x14;
	*ptr++ = 0x3;
	*ptr++ = 0x0;
	WILC_memcpy(ptr, strHostIfBASessionInfo->au8Bssid, ETH_ALEN);
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

	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
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
	WILC_memcpy(ptr, strHostIfBASessionInfo->au8Bssid, ETH_ALEN);
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
	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);

	if (strWID.ps8WidVal != NULL)
		WILC_FREE(strWID.ps8WidVal);

	return s32Error;

}


/*BugID_5222*/
/**
 *  @brief                      Handle_DelBASession
 *  @details            Delete block ack session
 *  @param[in]          tstrHostIFSetMulti* strHostIfSetMulti
 *  @return             NONE
 *  @author		Amr Abdel-Moghny
 *  @date			Feb. 2013
 *  @version		9.0
 */
static s32 Handle_DelBASession(void *drvHandler, tstrHostIfBASessionInfo *strHostIfBASessionInfo)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	char *ptr = NULL;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	PRINT_D(GENERIC_DBG, "Delete Block Ack session with\nBSSID = %.2x:%.2x:%.2x \nTID=%d\n",
		strHostIfBASessionInfo->au8Bssid[0],
		strHostIfBASessionInfo->au8Bssid[1],
		strHostIfBASessionInfo->au8Bssid[2],
		strHostIfBASessionInfo->u8Ted);

	strWID.u16WIDid = (u16)WID_11E_P_ACTION_REQ;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = (u8 *)WILC_MALLOC(BLOCK_ACK_REQ_SIZE);
	strWID.s32ValueSize = BLOCK_ACK_REQ_SIZE;
	ptr = strWID.ps8WidVal;
	/* *ptr++ = 0x14; */
	*ptr++ = 0x14;
	*ptr++ = 0x3;
	*ptr++ = 0x2;
	WILC_memcpy(ptr, strHostIfBASessionInfo->au8Bssid, ETH_ALEN);
	ptr += ETH_ALEN;
	*ptr++ = strHostIfBASessionInfo->u8Ted;
	/* BA direction = recipent*/
	*ptr++ = 0;
	/* Delba Reason */
	*ptr++ = 32; /* Unspecific QOS reason */

	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
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
	WILC_memcpy(ptr, strHostIfBASessionInfo->au8Bssid, ETH_ALEN);
	ptr += ETH_ALEN;
	/* TID*/
	*ptr++ = strHostIfBASessionInfo->u8Ted;

	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);

	if (strWID.ps8WidVal != NULL)
		WILC_FREE(strWID.ps8WidVal);

	/*BugID_5222*/
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
static s32 Handle_DelAllRxBASessions(void *drvHandler, tstrHostIfBASessionInfo *strHostIfBASessionInfo)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	char *ptr = NULL;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;

	PRINT_D(GENERIC_DBG, "Delete Block Ack session with\nBSSID = %.2x:%.2x:%.2x \nTID=%d\n",
		strHostIfBASessionInfo->au8Bssid[0],
		strHostIfBASessionInfo->au8Bssid[1],
		strHostIfBASessionInfo->au8Bssid[2],
		strHostIfBASessionInfo->u8Ted);

	strWID.u16WIDid = (u16)WID_DEL_ALL_RX_BA;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = (u8 *)WILC_MALLOC(BLOCK_ACK_REQ_SIZE);
	strWID.s32ValueSize = BLOCK_ACK_REQ_SIZE;
	ptr = strWID.ps8WidVal;
	*ptr++ = 0x14;
	*ptr++ = 0x3;
	*ptr++ = 0x2;
	WILC_memcpy(ptr, strHostIfBASessionInfo->au8Bssid, ETH_ALEN);
	ptr += ETH_ALEN;
	*ptr++ = strHostIfBASessionInfo->u8Ted;
	/* BA direction = recipent*/
	*ptr++ = 0;
	/* Delba Reason */
	*ptr++ = 32; /* Unspecific QOS reason */

	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	if (s32Error)
		PRINT_D(HOSTINF_DBG, "Couldn't delete BA Session\n");


	if (strWID.ps8WidVal != NULL)
		WILC_FREE(strWID.ps8WidVal);

	/*BugID_5222*/
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
	tstrHostIFmsg strHostIFmsg;
	tstrWILC_WFIDrv *pstrWFIDrv;

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	while (1) {
		WILC_MsgQueueRecv(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), &u32Ret, NULL);
		pstrWFIDrv = (tstrWILC_WFIDrv *)strHostIFmsg.drvHandler;
		if (strHostIFmsg.u16MsgId == HOST_IF_MSG_EXIT) {
			PRINT_D(GENERIC_DBG, "THREAD: Exiting HostIfThread\n");
			break;
		}


		/*Re-Queue HIF message*/
		if ((!g_wilc_initialized)) {
			PRINT_D(GENERIC_DBG, "--WAIT--");
			WILC_Sleep(200);
			WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
			continue;
		}

		if (strHostIFmsg.u16MsgId == HOST_IF_MSG_CONNECT && pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult != NULL) {
			PRINT_D(HOSTINF_DBG, "Requeue connect request till scan done received\n");
			WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
			WILC_Sleep(2);
			continue;
		}

		switch (strHostIFmsg.u16MsgId) {
		case HOST_IF_MSG_Q_IDLE:
			Handle_wait_msg_q_empty();
			break;

		case HOST_IF_MSG_SCAN:
			Handle_Scan(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIFscanAttr);
			break;

		case HOST_IF_MSG_CONNECT:
			Handle_Connect(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr);
			break;

		/*BugID_5137*/
		case HOST_IF_MSG_FLUSH_CONNECT:
			Handle_FlushConnect(strHostIFmsg.drvHandler);
			break;

		case HOST_IF_MSG_RCVD_NTWRK_INFO:
			Handle_RcvdNtwrkInfo(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strRcvdNetworkInfo);
			break;

		case HOST_IF_MSG_RCVD_GNRL_ASYNC_INFO:
			Handle_RcvdGnrlAsyncInfo(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strRcvdGnrlAsyncInfo);
			break;

		case HOST_IF_MSG_KEY:
			Handle_Key(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr);
			break;

		case HOST_IF_MSG_CFG_PARAMS:

			Handle_CfgParam(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIFCfgParamAttr);
			break;

		case HOST_IF_MSG_SET_CHANNEL:
			Handle_SetChannel(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIFSetChan);
			break;

		case HOST_IF_MSG_DISCONNECT:
			Handle_Disconnect(strHostIFmsg.drvHandler);
			break;

		case HOST_IF_MSG_RCVD_SCAN_COMPLETE:
			WILC_TimerStop(&(pstrWFIDrv->hScanTimer), NULL);
			PRINT_D(HOSTINF_DBG, "scan completed successfully\n");

			/*BugID_5213*/
			/*Allow chip sleep, only if both interfaces are not connected*/
			if (!linux_wlan_get_num_conn_ifcs()) {
				chip_sleep_manually(INFINITE_SLEEP_TIME);
			}

			Handle_ScanDone(strHostIFmsg.drvHandler, SCAN_EVENT_DONE);

				#ifdef WILC_P2P
			if (pstrWFIDrv->u8RemainOnChan_pendingreq)
				Handle_RemainOnChan(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIfRemainOnChan);
				#endif

			break;

		case HOST_IF_MSG_GET_RSSI:
			Handle_GetRssi(strHostIFmsg.drvHandler);
			break;

		case HOST_IF_MSG_GET_LINKSPEED:
			Handle_GetLinkspeed(strHostIFmsg.drvHandler);
			break;

		case HOST_IF_MSG_GET_STATISTICS:
			Handle_GetStatistics(strHostIFmsg.drvHandler, (tstrStatistics *)strHostIFmsg.uniHostIFmsgBody.pUserData);
			break;

		case HOST_IF_MSG_GET_CHNL:
			Handle_GetChnl(strHostIFmsg.drvHandler);
			break;

#ifdef WILC_AP_EXTERNAL_MLME
		case HOST_IF_MSG_ADD_BEACON:
			Handle_AddBeacon(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIFSetBeacon);
			break;

		case HOST_IF_MSG_DEL_BEACON:
			Handle_DelBeacon(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIFDelBeacon);
			break;

		case HOST_IF_MSG_ADD_STATION:
			Handle_AddStation(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strAddStaParam);
			break;

		case HOST_IF_MSG_DEL_STATION:
			Handle_DelStation(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strDelStaParam);
			break;

		case HOST_IF_MSG_EDIT_STATION:
			Handle_EditStation(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strEditStaParam);
			break;

		case HOST_IF_MSG_GET_INACTIVETIME:
			Handle_Get_InActiveTime(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIfStaInactiveT);
			break;

#endif /*WILC_AP_EXTERNAL_MLME*/
		case HOST_IF_MSG_SCAN_TIMER_FIRED:
			PRINT_D(HOSTINF_DBG, "Scan Timeout\n");

			Handle_ScanDone(strHostIFmsg.drvHandler, SCAN_EVENT_ABORTED);
			break;

		case HOST_IF_MSG_CONNECT_TIMER_FIRED:
			PRINT_D(HOSTINF_DBG, "Connect Timeout \n");
			Handle_ConnectTimeout(strHostIFmsg.drvHandler);
			break;

		case HOST_IF_MSG_POWER_MGMT:
			Handle_PowerManagement(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strPowerMgmtparam);
			break;

		case HOST_IF_MSG_SET_WFIDRV_HANDLER:
			Handle_SetWfiDrvHandler(&strHostIFmsg.uniHostIFmsgBody.strHostIfSetDrvHandler);
			break;

		case HOST_IF_MSG_SET_OPERATION_MODE:
			Handle_SetOperationMode(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIfSetOperationMode);
			break;

		case HOST_IF_MSG_SET_IPADDRESS:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_SET_IPADDRESS\n");
			Handle_set_IPAddress(strHostIFmsg.drvHandler, strHostIFmsg.uniHostIFmsgBody.strHostIfSetIP.au8IPAddr, strHostIFmsg.uniHostIFmsgBody.strHostIfSetIP.idx);
			break;

		case HOST_IF_MSG_GET_IPADDRESS:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_SET_IPADDRESS\n");
			Handle_get_IPAddress(strHostIFmsg.drvHandler, strHostIFmsg.uniHostIFmsgBody.strHostIfSetIP.au8IPAddr, strHostIFmsg.uniHostIFmsgBody.strHostIfSetIP.idx);
			break;

		/*BugID_5077*/
		case HOST_IF_MSG_SET_MAC_ADDRESS:
			Handle_SetMacAddress(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIfSetMacAddress);
			break;

		/*BugID_5213*/
		case HOST_IF_MSG_GET_MAC_ADDRESS:
			Handle_GetMacAddress(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIfGetMacAddress);
			break;

#ifdef WILC_P2P
		case HOST_IF_MSG_REMAIN_ON_CHAN:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_REMAIN_ON_CHAN\n");
			Handle_RemainOnChan(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIfRemainOnChan);
			break;

		case HOST_IF_MSG_REGISTER_FRAME:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_REGISTER_FRAME\n");
			Handle_RegisterFrame(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIfRegisterFrame);
			break;

		case HOST_IF_MSG_LISTEN_TIMER_FIRED:
			Handle_ListenStateExpired(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIfRemainOnChan);
			break;

			#endif
		case HOST_IF_MSG_SET_MULTICAST_FILTER:
			PRINT_D(HOSTINF_DBG, "HOST_IF_MSG_SET_MULTICAST_FILTER\n");
			Handle_SetMulticastFilter(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIfSetMulti);
			break;

		/*BugID_5222*/
		case HOST_IF_MSG_ADD_BA_SESSION:
			Handle_AddBASession(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIfBASessionInfo);
			break;

		case HOST_IF_MSG_DEL_ALL_RX_BA_SESSIONS:
			Handle_DelAllRxBASessions(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIfBASessionInfo);
			break;

		case HOST_IF_MSG_DEL_ALL_STA:
			Handle_DelAllSta(strHostIFmsg.drvHandler, &strHostIFmsg.uniHostIFmsgBody.strHostIFDelAllSta);
			break;

		default:
			PRINT_ER("[Host Interface] undefined Received Msg ID  \n");
			break;
		}
	}

	PRINT_D(HOSTINF_DBG, "Releasing thread exit semaphore\n");
	up(&hSemHostIFthrdEnd);
	return 0;
}

static void TimerCB_Scan(void *pvArg)
{
	tstrHostIFmsg strHostIFmsg;

	/* prepare the Timer Callback message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));
	strHostIFmsg.drvHandler = pvArg;
	strHostIFmsg.u16MsgId = HOST_IF_MSG_SCAN_TIMER_FIRED;

	/* send the message */
	WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
}

static void TimerCB_Connect(void *pvArg)
{
	tstrHostIFmsg strHostIFmsg;

	/* prepare the Timer Callback message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));
	strHostIFmsg.drvHandler = pvArg;
	strHostIFmsg.u16MsgId = HOST_IF_MSG_CONNECT_TIMER_FIRED;

	/* send the message */
	WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
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
s32 host_int_remove_key(WILC_WFIDrvHandle hWFIDrv, const u8 *pu8StaAddress)
{
	s32 s32Error = WILC_SUCCESS;
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
s32 host_int_remove_wep_key(WILC_WFIDrvHandle hWFIDrv, u8 u8keyIdx)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;


	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	/* prepare the Remove Wep Key Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));


	strHostIFmsg.u16MsgId = HOST_IF_MSG_KEY;
	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.enuKeyType = WEP;
	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.u8KeyAction = REMOVEKEY;
	strHostIFmsg.drvHandler = hWFIDrv;



	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx = u8keyIdx;

	/* send the message */
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error)
		PRINT_ER("Error in sending message queue : Request to remove WEP key \n");
	down(&(pstrWFIDrv->hSemTestKeyBlock));

	WILC_CATCH(s32Error)
	{

	}
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
s32 host_int_set_WEPDefaultKeyID(WILC_WFIDrvHandle hWFIDrv, u8 u8Index)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;


	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	/* prepare the Key Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));


	strHostIFmsg.u16MsgId = HOST_IF_MSG_KEY;
	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.enuKeyType = WEP;
	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.u8KeyAction = DEFAULTKEY;
	strHostIFmsg.drvHandler = hWFIDrv;


	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx = u8Index;

	/* send the message */
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error)
		PRINT_ER("Error in sending message queue : Default key index\n");
	down(&(pstrWFIDrv->hSemTestKeyBlock));

	WILC_CATCH(s32Error)
	{

	}

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
s32 host_int_add_wep_key_bss_sta(WILC_WFIDrvHandle hWFIDrv, const u8 *pu8WepKey, u8 u8WepKeylen, u8 u8Keyidx)
{

	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);

	}

	/* prepare the Key Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));


	strHostIFmsg.u16MsgId = HOST_IF_MSG_KEY;
	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.enuKeyType = WEP;
	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.u8KeyAction = ADDKEY;
	strHostIFmsg.drvHandler = hWFIDrv;


	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey = (u8 *)WILC_MALLOC(u8WepKeylen);

	WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey,
		    pu8WepKey, u8WepKeylen);


	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen = (u8WepKeylen);

	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx = u8Keyidx;

	/* send the message */
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error)
		PRINT_ER("Error in sending message queue :WEP Key\n");
	down(&(pstrWFIDrv->hSemTestKeyBlock));

	WILC_CATCH(s32Error)
	{

	}
	return s32Error;

}

#ifdef WILC_AP_EXTERNAL_MLME
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
s32 host_int_add_wep_key_bss_ap(WILC_WFIDrvHandle hWFIDrv, const u8 *pu8WepKey, u8 u8WepKeylen, u8 u8Keyidx, u8 u8mode, AUTHTYPE_T tenuAuth_type)
{

	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	u8 i;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);

	}

	/* prepare the Key Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	if (INFO) {
		for (i = 0; i < u8WepKeylen; i++)
			PRINT_INFO(HOSTAPD_DBG, "KEY is %x\n", pu8WepKey[i]);
	}
	strHostIFmsg.u16MsgId = HOST_IF_MSG_KEY;
	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.enuKeyType = WEP;
	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.u8KeyAction = ADDKEY_AP;
	strHostIFmsg.drvHandler = hWFIDrv;


	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey = (u8 *)WILC_MALLOC((u8WepKeylen));


	WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.uniHostIFkeyAttr.strHostIFwepAttr.pu8WepKey,
		    pu8WepKey, (u8WepKeylen));


	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwepAttr.u8WepKeylen = (u8WepKeylen);

	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwepAttr.u8Wepidx = u8Keyidx;

	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwepAttr.u8mode = u8mode;

	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwepAttr.tenuAuth_type = tenuAuth_type;
	/* send the message */
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);

	if (s32Error)
		PRINT_ER("Error in sending message queue :WEP Key\n");
	down(&(pstrWFIDrv->hSemTestKeyBlock));

	WILC_CATCH(s32Error)
	{

	}
	return s32Error;

}
#endif
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
s32 host_int_add_ptk(WILC_WFIDrvHandle hWFIDrv, const u8 *pu8Ptk, u8 u8PtkKeylen,
			     const u8 *mac_addr, const u8 *pu8RxMic, const u8 *pu8TxMic, u8 mode, u8 u8Ciphermode, u8 u8Idx)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	u8 u8KeyLen = u8PtkKeylen;
	u32 i;
	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}
	if (pu8RxMic != NULL) {
		u8KeyLen += RX_MIC_KEY_LEN;
	}
	if (pu8TxMic != NULL) {
		u8KeyLen += TX_MIC_KEY_LEN;
	}

	/* prepare the Key Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));


	strHostIFmsg.u16MsgId = HOST_IF_MSG_KEY;
	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.enuKeyType = WPAPtk;
	#ifdef WILC_AP_EXTERNAL_MLME
	if (mode == AP_MODE) {
		strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.u8KeyAction = ADDKEY_AP;
		strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
		uniHostIFkeyAttr.strHostIFwpaAttr.u8keyidx = u8Idx;
	}
	#endif
	if (mode == STATION_MODE)
		strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.u8KeyAction = ADDKEY;


	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwpaAttr.pu8key = (u8 *)WILC_MALLOC(u8PtkKeylen);


	WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.uniHostIFkeyAttr.strHostIFwpaAttr.pu8key,
		    pu8Ptk, u8PtkKeylen);

	if (pu8RxMic != NULL) {

		WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.uniHostIFkeyAttr.strHostIFwpaAttr.pu8key + 16,
			    pu8RxMic, RX_MIC_KEY_LEN);
		if (INFO) {
			for (i = 0; i < RX_MIC_KEY_LEN; i++)
				PRINT_INFO(CFG80211_DBG, "PairwiseRx[%d] = %x\n", i, pu8RxMic[i]);
		}
	}
	if (pu8TxMic != NULL) {

		WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.uniHostIFkeyAttr.strHostIFwpaAttr.pu8key + 24,
			    pu8TxMic, TX_MIC_KEY_LEN);
		if (INFO) {
			for (i = 0; i < TX_MIC_KEY_LEN; i++)
				PRINT_INFO(CFG80211_DBG, "PairwiseTx[%d] = %x\n", i, pu8TxMic[i]);
		}
	}

	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen = u8KeyLen;

	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwpaAttr.u8Ciphermode = u8Ciphermode;
	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwpaAttr.pu8macaddr = mac_addr;
	strHostIFmsg.drvHandler = hWFIDrv;

	/* send the message */
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);

	if (s32Error)
		PRINT_ER("Error in sending message queue:  PTK Key\n");

	/* ////////////// */
	down(&(pstrWFIDrv->hSemTestKeyBlock));
	/* WILC_Sleep(100); */
	/* /////// */

	WILC_CATCH(s32Error)
	{

	}

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
s32 host_int_add_rx_gtk(WILC_WFIDrvHandle hWFIDrv, const u8 *pu8RxGtk, u8 u8GtkKeylen,
				u8 u8KeyIdx, u32 u32KeyRSClen, const u8 *KeyRSC,
				const u8 *pu8RxMic, const u8 *pu8TxMic, u8 mode, u8 u8Ciphermode)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	u8 u8KeyLen = u8GtkKeylen;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}
	/* prepare the Key Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));


	if (pu8RxMic != NULL) {
		u8KeyLen += RX_MIC_KEY_LEN;
	}
	if (pu8TxMic != NULL) {
		u8KeyLen += TX_MIC_KEY_LEN;
	}
	if (KeyRSC != NULL) {
		strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
		uniHostIFkeyAttr.strHostIFwpaAttr.pu8seq = (u8 *)WILC_MALLOC(u32KeyRSClen);

		WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.uniHostIFkeyAttr.strHostIFwpaAttr.pu8seq,
			    KeyRSC, u32KeyRSClen);
	}


	strHostIFmsg.u16MsgId = HOST_IF_MSG_KEY;
	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.enuKeyType = WPARxGtk;
	strHostIFmsg.drvHandler = hWFIDrv;

    #ifdef WILC_AP_EXTERNAL_MLME
	if (mode == AP_MODE) {
		strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.u8KeyAction = ADDKEY_AP;
		strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.uniHostIFkeyAttr.strHostIFwpaAttr.u8Ciphermode = u8Ciphermode;
	}
    #endif
	if (mode == STATION_MODE)
		strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.u8KeyAction = ADDKEY;


	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwpaAttr.pu8key = (u8 *)WILC_MALLOC(u8KeyLen);

	WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.uniHostIFkeyAttr.strHostIFwpaAttr.pu8key,
		    pu8RxGtk, u8GtkKeylen);

	if (pu8RxMic != NULL) {

		WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.uniHostIFkeyAttr.strHostIFwpaAttr.pu8key + 16,
			    pu8RxMic, RX_MIC_KEY_LEN);

	}
	if (pu8TxMic != NULL) {

		WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.uniHostIFkeyAttr.strHostIFwpaAttr.pu8key + 24,
			    pu8TxMic, TX_MIC_KEY_LEN);

	}

	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwpaAttr.u8keyidx = u8KeyIdx;
	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwpaAttr.u8Keylen = u8KeyLen;

	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.
	uniHostIFkeyAttr.strHostIFwpaAttr.u8seqlen = u32KeyRSClen;



	/* send the message */
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error)
		PRINT_ER("Error in sending message queue:  RX GTK\n");
	/* ////////////// */
	down(&(pstrWFIDrv->hSemTestKeyBlock));
	/* WILC_Sleep(100); */
	/* /////// */

	WILC_CATCH(s32Error)
	{

	}
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
s32 host_int_set_pmkid_info(WILC_WFIDrvHandle hWFIDrv, tstrHostIFpmkidAttr *pu8PmkidInfoArray)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	u32 i;


	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	/* prepare the Key Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	strHostIFmsg.u16MsgId = HOST_IF_MSG_KEY;
	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.enuKeyType = PMKSA;
	strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.u8KeyAction = ADDKEY;
	strHostIFmsg.drvHandler = hWFIDrv;

	for (i = 0; i < pu8PmkidInfoArray->numpmkid; i++) {

		WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.uniHostIFkeyAttr.strHostIFpmkidAttr.pmkidlist[i].bssid, &pu8PmkidInfoArray->pmkidlist[i].bssid,
			    ETH_ALEN);

		WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFkeyAttr.uniHostIFkeyAttr.strHostIFpmkidAttr.pmkidlist[i].pmkid, &pu8PmkidInfoArray->pmkidlist[i].pmkid,
			    PMKID_LEN);
	}

	/* send the message */
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error)
		PRINT_ER(" Error in sending messagequeue: PMKID Info\n");

	WILC_CATCH(s32Error)
	{

	}

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
s32 host_int_get_pmkid_info(WILC_WFIDrvHandle hWFIDrv, u8 *pu8PmkidInfoArray,
				    u32 u32PmkidInfoLen)
{
	s32 s32Error = WILC_SUCCESS;
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
s32 host_int_set_RSNAConfigPSKPassPhrase(WILC_WFIDrvHandle hWFIDrv, u8 *pu8PassPhrase,
						 u8 u8Psklength)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	/* tstrWILC_WFIDrv * pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv; */

	/* u8 u8Psklength = WILC_strlen(pu8PassPhrase); */
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
s32 host_int_get_MacAddress(WILC_WFIDrvHandle hWFIDrv, u8 *pu8MacAddress)
{
	s32 s32Error = WILC_SUCCESS;
	tstrHostIFmsg strHostIFmsg;


	/* prepare the Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	strHostIFmsg.u16MsgId = HOST_IF_MSG_GET_MAC_ADDRESS;
	strHostIFmsg.uniHostIFmsgBody.strHostIfGetMacAddress.u8MacAddress = pu8MacAddress;
	strHostIFmsg.drvHandler = hWFIDrv;
	/* send the message */
	s32Error =	WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		PRINT_ER("Failed to send get mac address\n");
		return WILC_FAIL;
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
s32 host_int_set_MacAddress(WILC_WFIDrvHandle hWFIDrv, u8 *pu8MacAddress)
{
	s32 s32Error = WILC_SUCCESS;
	tstrHostIFmsg strHostIFmsg;

	PRINT_D(GENERIC_DBG, "mac addr = %x:%x:%x\n", pu8MacAddress[0], pu8MacAddress[1], pu8MacAddress[2]);

	/* prepare setting mac address message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));
	strHostIFmsg.u16MsgId = HOST_IF_MSG_SET_MAC_ADDRESS;
	WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIfSetMacAddress.u8MacAddress, pu8MacAddress, ETH_ALEN);
	strHostIFmsg.drvHandler = hWFIDrv;

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		PRINT_ER("Failed to send message queue: Set mac address\n");
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{

	}

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
s32 host_int_get_RSNAConfigPSKPassPhrase(WILC_WFIDrvHandle hWFIDrv,
						 u8 *pu8PassPhrase, u8 u8Psklength)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	/* tstrWILC_WFIDrv * pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv; */

	strWID.u16WIDid	= (u16)WID_11I_PSK;
	strWID.enuWIDtype	= WID_STR;
	strWID.s32ValueSize = u8Psklength;
	strWID.ps8WidVal	= pu8PassPhrase;

	return s32Error;
}

/**
 *  @brief                      host_int_get_site_survey_results
 *  @details            gets the site survey results
 *  @param[in,out] handle to the wifi driver,
 *                                Message containing  site survey results in the
 *                                following format
 *|---------------------------------------------------|
 | MsgLength | fragNo.	| MsgBodyLength	| MsgBody	|
 ||-----------|-----------|---------------|-----------|
 |	 1		|	  1		|		1		|	 1		|
 | -----------------------------------------	 |  ----------------
 |
 ||---------------------------------------|
 | Network1 | Netweork2 | ... | Network5 |
 ||---------------------------------------|
 |	44	   |	44	   | ... |	 44		|
 | -------------------------- | ---------------------------------------
 |
 ||---------------------------------------------------------------------|
 | SSID | BSS Type | Channel | Security Status| BSSID | RSSI |Reserved |
 |
 |
 ||------|----------|---------|----------------|-------|------|---------|
 |  33  |	 1	  |	  1		|		1		 |	  6	 |	 1	|	 1	  |
 ||---------------------------------------------------------------------|
 *  @return             Error code indicating success/failure
 *  @note
 *  @author		zsalah
 *  @date		8 March 2012
 *  @version		1.0
 */
#ifndef CONNECT_DIRECT
s32 host_int_get_site_survey_results(WILC_WFIDrvHandle hWFIDrv,
					     u8 ppu8RcvdSiteSurveyResults[][MAX_SURVEY_RESULT_FRAG_SIZE],
					     u32 u32MaxSiteSrvyFragLen)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID astrWIDList[2];
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;

	astrWIDList[0].u16WIDid = (u16)WID_SITE_SURVEY_RESULTS;
	astrWIDList[0].enuWIDtype = WID_STR;
	astrWIDList[0].ps8WidVal = ppu8RcvdSiteSurveyResults[0];
	astrWIDList[0].s32ValueSize = u32MaxSiteSrvyFragLen;

	astrWIDList[1].u16WIDid = (u16)WID_SITE_SURVEY_RESULTS;
	astrWIDList[1].enuWIDtype = WID_STR;
	astrWIDList[1].ps8WidVal = ppu8RcvdSiteSurveyResults[1];
	astrWIDList[1].s32ValueSize = u32MaxSiteSrvyFragLen;

	s32Error = SendConfigPkt(GET_CFG, astrWIDList, 2, true, (u32)pstrWFIDrv);

	/*get the value by searching the local copy*/
	if (s32Error) {
		PRINT_ER("Failed to send config packet to get survey results\n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
	}

	WILC_CATCH(s32Error)
	{

	}

	return s32Error;
}
#endif

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
s32 host_int_set_start_scan_req(WILC_WFIDrvHandle hWFIDrv, u8 scanSource)
{
	s32 s32Error = WILC_SUCCESS;
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

s32 host_int_get_start_scan_req(WILC_WFIDrvHandle hWFIDrv, u8 *pu8ScanSource)
{
	s32 s32Error = WILC_SUCCESS;
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
s32 host_int_set_join_req(WILC_WFIDrvHandle hWFIDrv, u8 *pu8bssid,
				  const u8 *pu8ssid, size_t ssidLen,
				  const u8 *pu8IEs, size_t IEsLen,
				  tWILCpfConnectResult pfConnectResult, void *pvUserArg,
				  u8 u8security, AUTHTYPE_T tenuAuth_type,
				  u8 u8channel,
				  void *pJoinParams)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	tenuScanConnTimer enuScanConnTimer;

	if (pstrWFIDrv == NULL || pfConnectResult == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	if (hWFIDrv == NULL) {
		PRINT_ER("Driver not initialized: gWFiDrvHandle = NULL\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	if (pJoinParams == NULL) {
		PRINT_ER("Unable to Join - JoinParams is NULL\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);

	}
/*
 *      if(gWFiDrvHandle->strWILC_UsrScanReq.u32RcvdChCount == 0)
 *      {
 *              PRINT_ER("No scan results exist: Scanning should be done\n");
 *              WILC_ERRORREPORT(s32Error, WILC_FAIL);
 *      }
 */
	/* prepare the Connect Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	strHostIFmsg.u16MsgId = HOST_IF_MSG_CONNECT;

	strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr.u8security = u8security;
	strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr.tenuAuth_type = tenuAuth_type;
	strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr.u8channel = u8channel;
	strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr.pfConnectResult = pfConnectResult;
	strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr.pvUserArg = pvUserArg;
	strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr.pJoinParams = pJoinParams;
	strHostIFmsg.drvHandler = hWFIDrv;

	if (pu8bssid != NULL) {
		strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr.pu8bssid = (u8 *)WILC_MALLOC(6); /* will be deallocated by the receiving thread */
		WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr.pu8bssid,
			    pu8bssid, 6);
	}

	if (pu8ssid != NULL) {
		strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr.ssidLen = ssidLen;
		strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr.pu8ssid = (u8 *)WILC_MALLOC(ssidLen); /* will be deallocated by the receiving thread */
		WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr.pu8ssid,

			    pu8ssid, ssidLen);
	}

	if (pu8IEs != NULL) {
		strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr.IEsLen = IEsLen;
		strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr.pu8IEs = (u8 *)WILC_MALLOC(IEsLen); /* will be deallocated by the receiving thread */
		WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFconnectAttr.pu8IEs,
			    pu8IEs, IEsLen);
	}
	if (pstrWFIDrv->enuHostIFstate < HOST_IF_CONNECTING) {
		pstrWFIDrv->enuHostIFstate = HOST_IF_CONNECTING;
	} else
		PRINT_D(GENERIC_DBG, "Don't set state to 'connecting' as state is %d\n", pstrWFIDrv->enuHostIFstate);

	/* send the message */
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		PRINT_ER("Failed to send message queue: Set join request\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	enuScanConnTimer = CONNECT_TIMER;
	WILC_TimerStart(&(pstrWFIDrv->hConnectTimer), HOST_IF_CONNECT_TIMEOUT, (void *) hWFIDrv, NULL);

	WILC_CATCH(s32Error)
	{

	}

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

s32 host_int_flush_join_req(WILC_WFIDrvHandle hWFIDrv)
{
	s32 s32Error = WILC_SUCCESS;
	tstrHostIFmsg strHostIFmsg;

	if (!gu8FlushedJoinReq)	{
		s32Error = WILC_FAIL;
		return s32Error;
	}


	if (hWFIDrv  == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}


	strHostIFmsg.u16MsgId = HOST_IF_MSG_FLUSH_CONNECT;
	strHostIFmsg.drvHandler = hWFIDrv;

	/* send the message */
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		PRINT_ER("Failed to send message queue: Flush join request\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	WILC_CATCH(s32Error)
	{

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
s32 host_int_disconnect(WILC_WFIDrvHandle hWFIDrv, u16 u16ReasonCode)
{
	s32 s32Error = WILC_SUCCESS;
	tstrHostIFmsg strHostIFmsg;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("Driver not initialized: pstrWFIDrv = NULL \n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	if (pstrWFIDrv == NULL)	{
		PRINT_ER("gWFiDrvHandle = NULL\n");
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	/* prepare the Disconnect Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	strHostIFmsg.u16MsgId = HOST_IF_MSG_DISCONNECT;
	strHostIFmsg.drvHandler = hWFIDrv;

	/* send the message */
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error)
		PRINT_ER("Failed to send message queue: disconnect\n");
	/* ////////////// */
	down(&(pstrWFIDrv->hSemTestDisconnectBlock));
	/* /////// */

	WILC_CATCH(s32Error)
	{

	}

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
s32 host_int_disconnect_station(WILC_WFIDrvHandle hWFIDrv, u8 assoc_id)
{
	s32 s32Error = WILC_SUCCESS;
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

s32 host_int_get_assoc_req_info(WILC_WFIDrvHandle hWFIDrv, u8 *pu8AssocReqInfo,
					u32 u32AssocReqInfoLen)
{
	s32 s32Error = WILC_SUCCESS;
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
s32 host_int_get_assoc_res_info(WILC_WFIDrvHandle hWFIDrv, u8 *pu8AssocRespInfo,
					u32 u32MaxAssocRespInfoLen, u32 *pu32RcvdAssocRespInfoLen)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("Driver not initialized: pstrWFIDrv = NULL \n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	strWID.u16WIDid = (u16)WID_ASSOC_RES_INFO;
	strWID.enuWIDtype = WID_STR;
	strWID.ps8WidVal = pu8AssocRespInfo;
	strWID.s32ValueSize = u32MaxAssocRespInfoLen;


	/* Sending Configuration packet */
	s32Error = SendConfigPkt(GET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	if (s32Error) {
		PRINT_ER("Failed to send association response config packet\n");
		*pu32RcvdAssocRespInfoLen = 0;
		WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
	} else {
		*pu32RcvdAssocRespInfoLen = strWID.s32ValueSize;
	}

	WILC_CATCH(s32Error)
	{

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
s32 host_int_get_rx_power_level(WILC_WFIDrvHandle hWFIDrv, u8 *pu8RxPowerLevel,
					u32 u32RxPowerLevelLen)
{
	s32 s32Error = WILC_SUCCESS;
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
s32 host_int_set_mac_chnl_num(WILC_WFIDrvHandle hWFIDrv, u8 u8ChNum)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	/* prepare the set channel message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));
	strHostIFmsg.u16MsgId = HOST_IF_MSG_SET_CHANNEL;
	strHostIFmsg.uniHostIFmsgBody.strHostIFSetChan.u8SetChan = u8ChNum;
	strHostIFmsg.drvHandler = hWFIDrv;

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{

	}

	return s32Error;
}


s32 host_int_wait_msg_queue_idle(void)
{
	s32 s32Error = WILC_SUCCESS;

	tstrHostIFmsg strHostIFmsg;

	/* prepare the set driver handler message */

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));
	strHostIFmsg.u16MsgId = HOST_IF_MSG_Q_IDLE;
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{

	}

	/* wait untill MSG Q is empty */
	down(&hWaitResponse);

	return s32Error;

}

s32 host_int_set_wfi_drv_handler(u32 u32address)
{
	s32 s32Error = WILC_SUCCESS;

	tstrHostIFmsg strHostIFmsg;


	/* prepare the set driver handler message */

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));
	strHostIFmsg.u16MsgId = HOST_IF_MSG_SET_WFIDRV_HANDLER;
	strHostIFmsg.uniHostIFmsgBody.strHostIfSetDrvHandler.u32Address = u32address;
	/* strHostIFmsg.drvHandler=hWFIDrv; */

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{

	}

	return s32Error;
}



s32 host_int_set_operation_mode(WILC_WFIDrvHandle hWFIDrv, u32 u32mode)
{
	s32 s32Error = WILC_SUCCESS;

	tstrHostIFmsg strHostIFmsg;


	/* prepare the set driver handler message */

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));
	strHostIFmsg.u16MsgId = HOST_IF_MSG_SET_OPERATION_MODE;
	strHostIFmsg.uniHostIFmsgBody.strHostIfSetOperationMode.u32Mode = u32mode;
	strHostIFmsg.drvHandler = hWFIDrv;

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{

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
s32 host_int_get_host_chnl_num(WILC_WFIDrvHandle hWFIDrv, u8 *pu8ChNo)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("Driver not initialized: pstrWFIDrv = NULL \n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	/* prepare the Get Channel Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	strHostIFmsg.u16MsgId = HOST_IF_MSG_GET_CHNL;
	strHostIFmsg.drvHandler = hWFIDrv;

	/* send the message */
	s32Error =	WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error)
		PRINT_ER("Failed to send get host channel param's message queue ");
	down(&(pstrWFIDrv->hSemGetCHNL));
	/* gu8Chnl = 11; */

	*pu8ChNo = gu8Chnl;

	WILC_CATCH(s32Error)
	{
	}

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
s32 host_int_test_set_int_wid(WILC_WFIDrvHandle hWFIDrv, u32 u32TestMemAddr)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWID	strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;


	if (pstrWFIDrv == NULL) {
		PRINT_ER("Driver not initialized: pstrWFIDrv = NULL \n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	/*prepare configuration packet*/
	strWID.u16WIDid = (u16)WID_MEMORY_ADDRESS;
	strWID.enuWIDtype = WID_INT;
	strWID.ps8WidVal = (char *)&u32TestMemAddr;
	strWID.s32ValueSize = sizeof(u32);

	/*Sending Cfg*/
	s32Error = SendConfigPkt(SET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	if (s32Error) {
		PRINT_ER("Test Function: Failed to set wid value\n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
	} else {
		PRINT_D(HOSTINF_DBG, "Successfully set wid value\n");

	}

	WILC_CATCH(s32Error)
	{

	}
	return s32Error;
}

#ifdef WILC_AP_EXTERNAL_MLME
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
s32 host_int_get_inactive_time(WILC_WFIDrvHandle hWFIDrv, const u8 *mac, u32 *pu32InactiveTime)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;

	if (pstrWFIDrv == NULL) {
		PRINT_ER("Driver not initialized: pstrWFIDrv = NULL \n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));


	WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIfStaInactiveT.mac,
		    mac, ETH_ALEN);

	strHostIFmsg.u16MsgId = HOST_IF_MSG_GET_INACTIVETIME;
	strHostIFmsg.drvHandler = hWFIDrv;

	/* send the message */
	s32Error =	WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error)
		PRINT_ER("Failed to send get host channel param's message queue ");

	down(&(pstrWFIDrv->hSemInactiveTime));

	*pu32InactiveTime = gu32InactiveTime;

	WILC_CATCH(s32Error)
	{
	}

	return s32Error;
}
#endif
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
s32 host_int_test_get_int_wid(WILC_WFIDrvHandle hWFIDrv, u32 *pu32TestMemAddr)
{

	s32 s32Error = WILC_SUCCESS;
	tstrWID	strWID;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;


	if (pstrWFIDrv == NULL) {
		PRINT_ER("Driver not initialized: pstrWFIDrv = NULL \n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	strWID.u16WIDid = (u16)WID_MEMORY_ADDRESS;
	strWID.enuWIDtype = WID_INT;
	strWID.ps8WidVal = (s8 *)pu32TestMemAddr;
	strWID.s32ValueSize = sizeof(u32);

	s32Error = SendConfigPkt(GET_CFG, &strWID, 1, true, (u32)pstrWFIDrv);
	/*get the value by searching the local copy*/
	if (s32Error) {
		PRINT_ER("Test Function: Failed to get wid value\n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_STATE);
	} else {
		PRINT_D(HOSTINF_DBG, "Successfully got wid value\n");

	}

	WILC_CATCH(s32Error)
	{

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
s32 host_int_get_rssi(WILC_WFIDrvHandle hWFIDrv, s8 *ps8Rssi)
{
	s32 s32Error = WILC_SUCCESS;
	tstrHostIFmsg strHostIFmsg;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;


	/* prepare the Get RSSI Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	strHostIFmsg.u16MsgId = HOST_IF_MSG_GET_RSSI;
	strHostIFmsg.drvHandler = hWFIDrv;

	/* send the message */
	s32Error =	WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		PRINT_ER("Failed to send get host channel param's message queue ");
		return WILC_FAIL;
	}

	down(&(pstrWFIDrv->hSemGetRSSI));


	if (ps8Rssi == NULL) {
		PRINT_ER("RSS pointer value is null");
		return WILC_FAIL;
	}


	*ps8Rssi = gs8Rssi;


	return s32Error;
}

s32 host_int_get_link_speed(WILC_WFIDrvHandle hWFIDrv, s8 *ps8lnkspd)
{
	tstrHostIFmsg strHostIFmsg;
	s32 s32Error = WILC_SUCCESS;

	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;



	/* prepare the Get LINKSPEED Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	strHostIFmsg.u16MsgId = HOST_IF_MSG_GET_LINKSPEED;
	strHostIFmsg.drvHandler = hWFIDrv;

	/* send the message */
	s32Error =	WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		PRINT_ER("Failed to send GET_LINKSPEED to message queue ");
		return WILC_FAIL;
	}

	down(&(pstrWFIDrv->hSemGetLINKSPEED));


	if (ps8lnkspd == NULL) {
		PRINT_ER("LINKSPEED pointer value is null");
		return WILC_FAIL;
	}


	*ps8lnkspd = gs8lnkspd;


	return s32Error;
}

s32 host_int_get_statistics(WILC_WFIDrvHandle hWFIDrv, tstrStatistics *pstrStatistics)
{
	s32 s32Error = WILC_SUCCESS;
	tstrHostIFmsg strHostIFmsg;


	/* prepare the Get RSSI Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	strHostIFmsg.u16MsgId = HOST_IF_MSG_GET_STATISTICS;
	strHostIFmsg.uniHostIFmsgBody.pUserData = (char *)pstrStatistics;
	strHostIFmsg.drvHandler = hWFIDrv;
	/* send the message */
	s32Error =	WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		PRINT_ER("Failed to send get host channel param's message queue ");
		return WILC_FAIL;
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
s32 host_int_scan(WILC_WFIDrvHandle hWFIDrv, u8 u8ScanSource,
			  u8 u8ScanType, u8 *pu8ChnlFreqList,
			  u8 u8ChnlListLen, const u8 *pu8IEs,
			  size_t IEsLen, tWILCpfScanResult ScanResult,
			  void *pvUserArg, tstrHiddenNetwork  *pstrHiddenNetwork)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	tenuScanConnTimer enuScanConnTimer;

	if (pstrWFIDrv == NULL || ScanResult == NULL)	{
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}


	/* prepare the Scan Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	strHostIFmsg.u16MsgId = HOST_IF_MSG_SCAN;

	if (pstrHiddenNetwork != NULL) {
		strHostIFmsg.uniHostIFmsgBody.strHostIFscanAttr.strHiddenNetwork.pstrHiddenNetworkInfo = pstrHiddenNetwork->pstrHiddenNetworkInfo;
		strHostIFmsg.uniHostIFmsgBody.strHostIFscanAttr.strHiddenNetwork.u8ssidnum = pstrHiddenNetwork->u8ssidnum;

	} else
		PRINT_D(HOSTINF_DBG, "pstrHiddenNetwork IS EQUAL TO NULL\n");

	strHostIFmsg.drvHandler = hWFIDrv;
	strHostIFmsg.uniHostIFmsgBody.strHostIFscanAttr.u8ScanSource = u8ScanSource;
	strHostIFmsg.uniHostIFmsgBody.strHostIFscanAttr.u8ScanType = u8ScanType;
	strHostIFmsg.uniHostIFmsgBody.strHostIFscanAttr.pfScanResult = ScanResult;
	strHostIFmsg.uniHostIFmsgBody.strHostIFscanAttr.pvUserArg = pvUserArg;

	strHostIFmsg.uniHostIFmsgBody.strHostIFscanAttr.u8ChnlListLen = u8ChnlListLen;
	strHostIFmsg.uniHostIFmsgBody.strHostIFscanAttr.pu8ChnlFreqList = (u8 *)WILC_MALLOC(u8ChnlListLen);        /* will be deallocated by the receiving thread */
	WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFscanAttr.pu8ChnlFreqList,
		    pu8ChnlFreqList, u8ChnlListLen);

	strHostIFmsg.uniHostIFmsgBody.strHostIFscanAttr.IEsLen = IEsLen;
	strHostIFmsg.uniHostIFmsgBody.strHostIFscanAttr.pu8IEs = (u8 *)WILC_MALLOC(IEsLen);        /* will be deallocated by the receiving thread */
	WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strHostIFscanAttr.pu8IEs,
		    pu8IEs, IEsLen);

	/* send the message */
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		PRINT_ER("Error in sending message queue scanning parameters: Error(%d)\n", s32Error);
		WILC_ERRORREPORT(s32Error, WILC_FAIL);
	}

	enuScanConnTimer = SCAN_TIMER;
	PRINT_D(HOSTINF_DBG, ">> Starting the SCAN timer\n");
	WILC_TimerStart(&(pstrWFIDrv->hScanTimer), HOST_IF_SCAN_TIMEOUT, (void *) hWFIDrv, NULL);


	WILC_CATCH(s32Error)
	{

	}
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
s32 hif_set_cfg(WILC_WFIDrvHandle hWFIDrv, tstrCfgParamVal *pstrCfgParamVal)
{

	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;

	tstrHostIFmsg strHostIFmsg;


	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}
	/* prepare the WiphyParams Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));
	strHostIFmsg.u16MsgId = HOST_IF_MSG_CFG_PARAMS;
	strHostIFmsg.uniHostIFmsgBody.strHostIFCfgParamAttr.pstrCfgParamVal = *pstrCfgParamVal;
	strHostIFmsg.drvHandler = hWFIDrv;

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);

	WILC_CATCH(s32Error)
	{
	}

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
s32 hif_get_cfg(WILC_WFIDrvHandle hWFIDrv, u16 u16WID, u16 *pu16WID_Value)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;

	down(&(pstrWFIDrv->gtOsCfgValuesSem));

	if (pstrWFIDrv == NULL) {
		PRINT_ER("Driver not initialized: pstrWFIDrv = NULL \n");
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
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

	WILC_CATCH(s32Error)
	{
	}
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

void GetPeriodicRSSI(void *pvArg)
{
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)pvArg;
	if (pstrWFIDrv == NULL)	{
		PRINT_ER("Driver handler is NULL\n");
		return;
	}

	if (pstrWFIDrv->enuHostIFstate == HOST_IF_CONNECTED) {
		s32 s32Error = WILC_SUCCESS;
		tstrHostIFmsg strHostIFmsg;

		/* prepare the Get RSSI Message */
		WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

		strHostIFmsg.u16MsgId = HOST_IF_MSG_GET_RSSI;
		strHostIFmsg.drvHandler = pstrWFIDrv;

		/* send the message */
		s32Error =	WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
		if (s32Error) {
			PRINT_ER("Failed to send get host channel param's message queue ");
			return;
		}
	}
	WILC_TimerStart(&(g_hPeriodicRSSI), 5000, (void *)pstrWFIDrv, NULL);
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
static u32 u32Intialized;
static u32 msgQ_created;
static u32 clients_count;

s32 host_int_init(WILC_WFIDrvHandle *phWFIDrv)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv;

	/*if(u32Intialized == 1)
	 * {
	 *      PRINT_D(HOSTINF_DBG,"Host interface is previously initialized\n");
	 * *phWFIDrv = (WILC_WFIDrvHandle)gWFiDrvHandle; //Will be adjusted later for P2P
	 *      return 0;
	 * }	*/
	PRINT_D(HOSTINF_DBG, "Initializing host interface for client %d\n", clients_count + 1);

	gbScanWhileConnected = false;

	sema_init(&hWaitResponse, 0);



	/*Allocate host interface private structure*/
	pstrWFIDrv  = (tstrWILC_WFIDrv *)WILC_MALLOC(sizeof(tstrWILC_WFIDrv));
	if (pstrWFIDrv == NULL) {
		/* WILC_ERRORREPORT(s32Error,WILC_NO_MEM); */
		s32Error = WILC_NO_MEM;
		PRINT_ER("Failed to allocate memory\n");
		goto _fail_timer_2;
	}
	WILC_memset(pstrWFIDrv, 0, sizeof(tstrWILC_WFIDrv));
	/*return driver handle to user*/
	*phWFIDrv = (WILC_WFIDrvHandle)pstrWFIDrv;
	/*save into globl handle*/

	#ifdef DISABLE_PWRSAVE_AND_SCAN_DURING_IP

	g_obtainingIP = false;
	#endif

	PRINT_D(HOSTINF_DBG, "Global handle pointer value=%p\n", pstrWFIDrv);
	/* /////////////////////////////////////// */
	if (clients_count == 0)	{
		sema_init(&hSemHostIFthrdEnd, 0);
		sema_init(&hSemDeinitDrvHandle, 0);
		/*BugID_5348*/
		sema_init(&hSemHostIntDeinit, 1);
	}

	sema_init(&(pstrWFIDrv->hSemTestKeyBlock), 0);
	sema_init(&(pstrWFIDrv->hSemTestDisconnectBlock), 0);
	sema_init(&(pstrWFIDrv->hSemGetRSSI), 0);
	sema_init(&(pstrWFIDrv->hSemGetLINKSPEED), 0);
	sema_init(&(pstrWFIDrv->hSemGetCHNL), 0);
	sema_init(&(pstrWFIDrv->hSemInactiveTime), 0);

	/* /////////////////////////////////////// */



	PRINT_D(HOSTINF_DBG, "INIT: CLIENT COUNT %d\n", clients_count);

	if (clients_count == 0)	{

		s32Error = WILC_MsgQueueCreate(&gMsgQHostIF, NULL);


		if (s32Error < 0) {
			PRINT_ER("Failed to creat MQ\n");
			goto _fail_;
		}
		msgQ_created = 1;
		HostIFthreadHandler = kthread_run(hostIFthread, NULL, "WILC_kthread");
		if (IS_ERR(HostIFthreadHandler)) {
			PRINT_ER("Failed to creat Thread\n");
			s32Error = WILC_FAIL;
			goto _fail_mq_;
		}
		s32Error = WILC_TimerCreate(&(g_hPeriodicRSSI), GetPeriodicRSSI, NULL);
		if (s32Error < 0) {
			PRINT_ER("Failed to creat Timer\n");
			goto _fail_timer_1;
		}
		WILC_TimerStart(&(g_hPeriodicRSSI), 5000, (void *)pstrWFIDrv, NULL);

	}


	s32Error = WILC_TimerCreate(&(pstrWFIDrv->hScanTimer), TimerCB_Scan, NULL);
	if (s32Error < 0) {
		PRINT_ER("Failed to creat Timer\n");
		goto _fail_thread_;
	}

	s32Error = WILC_TimerCreate(&(pstrWFIDrv->hConnectTimer), TimerCB_Connect, NULL);
	if (s32Error < 0) {
		PRINT_ER("Failed to creat Timer\n");
		goto _fail_timer_1;
	}


	#ifdef WILC_P2P
	/*Remain on channel timer*/
	s32Error = WILC_TimerCreate(&(pstrWFIDrv->hRemainOnChannel), ListenTimerCB, NULL);
	if (s32Error < 0) {
		PRINT_ER("Failed to creat Remain-on-channel Timer\n");
		goto _fail_timer_3;
	}
	#endif

	sema_init(&(pstrWFIDrv->gtOsCfgValuesSem), 1);
	down(&(pstrWFIDrv->gtOsCfgValuesSem));



#ifdef SIMULATION
	TransportInit();
#endif

	pstrWFIDrv->enuHostIFstate = HOST_IF_IDLE;
	/* gWFiDrvHandle->bPendingConnRequest = false; */

	/*Initialize CFG WIDS Defualt Values*/

	pstrWFIDrv->strCfgValues.site_survey_enabled = SITE_SURVEY_OFF;
	pstrWFIDrv->strCfgValues.scan_source = DEFAULT_SCAN;
	pstrWFIDrv->strCfgValues.active_scan_time = ACTIVE_SCAN_TIME;
	pstrWFIDrv->strCfgValues.passive_scan_time = PASSIVE_SCAN_TIME;
	pstrWFIDrv->strCfgValues.curr_tx_rate = AUTORATE;


	#ifdef WILC_P2P

	pstrWFIDrv->u64P2p_MgmtTimeout = 0;

	#endif

	PRINT_INFO(HOSTINF_DBG, "Initialization values, Site survey value: %d\n Scan source: %d\n Active scan time: %d\n Passive scan time: %d\nCurrent tx Rate = %d\n",

		   pstrWFIDrv->strCfgValues.site_survey_enabled, pstrWFIDrv->strCfgValues.scan_source,
		   pstrWFIDrv->strCfgValues.active_scan_time, pstrWFIDrv->strCfgValues.passive_scan_time,
		   pstrWFIDrv->strCfgValues.curr_tx_rate);


	up(&(pstrWFIDrv->gtOsCfgValuesSem));

	/*TODO Code to setup simulation to be removed later*/
	/*Intialize configurator module*/
	s32Error = CoreConfiguratorInit();
	if (s32Error < 0) {
		PRINT_ER("Failed to initialize core configurator\n");
		goto _fail_mem_;
	}

#ifdef SIMULATION
	/*Initialize Simulaor*/
	CoreConfigSimulatorInit();
#endif

	u32Intialized = 1;
	clients_count++; /* increase number of created entities */

	return s32Error;


_fail_mem_:
	if (pstrWFIDrv != NULL)
		WILC_FREE(pstrWFIDrv);
#ifdef WILC_P2P
_fail_timer_3:
	WILC_TimerDestroy(&(pstrWFIDrv->hRemainOnChannel), NULL);
#endif
_fail_timer_2:
	up(&(pstrWFIDrv->gtOsCfgValuesSem));
	WILC_TimerDestroy(&(pstrWFIDrv->hConnectTimer), NULL);
_fail_timer_1:
	WILC_TimerDestroy(&(pstrWFIDrv->hScanTimer), NULL);
_fail_thread_:
	kthread_stop(HostIFthreadHandler);
_fail_mq_:
	WILC_MsgQueueDestroy(&gMsgQHostIF, NULL);
_fail_:
	return s32Error;


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

s32 host_int_deinit(WILC_WFIDrvHandle hWFIDrv)
{
	s32 s32Error = WILC_SUCCESS;
	tstrHostIFmsg strHostIFmsg;


	/*obtain driver handle*/
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	/*if(u32Intialized == 0)
	 * {
	 *      PRINT_ER("Host Interface is not initialized\n");
	 *      return 0;
	 * }*/

	/*BugID_5348*/

	if (pstrWFIDrv == NULL)	{
		PRINT_ER("pstrWFIDrv = NULL\n");
		return 0;
	}

	down(&hSemHostIntDeinit);

	terminated_handle = pstrWFIDrv;
	PRINT_D(HOSTINF_DBG, "De-initializing host interface for client %d\n", clients_count);

	/*BugID_5348*/
	/*Destroy all timers before acquiring hSemDeinitDrvHandle*/
	/*to guarantee handling all messages befor proceeding*/
	if (WILC_TimerDestroy(&(pstrWFIDrv->hScanTimer), NULL)) {
		PRINT_D(HOSTINF_DBG, ">> Scan timer is active \n");
		/* msleep(HOST_IF_SCAN_TIMEOUT+1000); */
	}

	if (WILC_TimerDestroy(&(pstrWFIDrv->hConnectTimer), NULL)) {
		PRINT_D(HOSTINF_DBG, ">> Connect timer is active \n");
		/* msleep(HOST_IF_CONNECT_TIMEOUT+1000); */
	}


	if (WILC_TimerDestroy(&(g_hPeriodicRSSI), NULL)) {
		PRINT_D(HOSTINF_DBG, ">> Connect timer is active \n");
		/* msleep(HOST_IF_CONNECT_TIMEOUT+1000); */
	}

	#ifdef WILC_P2P
	/*Destroy Remain-onchannel Timer*/
	WILC_TimerDestroy(&(pstrWFIDrv->hRemainOnChannel), NULL);
	#endif

	host_int_set_wfi_drv_handler((u32)NULL);
	down(&hSemDeinitDrvHandle);


	/*Calling the CFG80211 scan done function with the abort flag set to true*/
	if (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult) {
		pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult(SCAN_EVENT_ABORTED, NULL,
								pstrWFIDrv->strWILC_UsrScanReq.u32UserScanPvoid, NULL);

		pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult = NULL;
	}
	/*deinit configurator and simulator*/
#ifdef SIMULATION
	CoreConfigSimulatorDeInit();
#endif
	CoreConfiguratorDeInit();
#ifdef SIMULATION
	TransportDeInit();
#endif

	pstrWFIDrv->enuHostIFstate = HOST_IF_IDLE;

	gbScanWhileConnected = false;

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	if (clients_count == 1)	{
		if (WILC_TimerDestroy(&g_hPeriodicRSSI, NULL)) {
			PRINT_D(HOSTINF_DBG, ">> Connect timer is active \n");
			/* msleep(HOST_IF_CONNECT_TIMEOUT+1000); */
		}
		strHostIFmsg.u16MsgId = HOST_IF_MSG_EXIT;
		strHostIFmsg.drvHandler = hWFIDrv;


		s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
		if (s32Error != WILC_SUCCESS) {
			PRINT_ER("Error in sending deinit's message queue message function: Error(%d)\n", s32Error);
		}

		down(&hSemHostIFthrdEnd);



		WILC_MsgQueueDestroy(&gMsgQHostIF, NULL);
		msgQ_created = 0;
	}

	down(&(pstrWFIDrv->gtOsCfgValuesSem));

	/*Setting the gloabl driver handler with NULL*/
	u32Intialized = 0;
	/* gWFiDrvHandle = NULL; */
	if (pstrWFIDrv != NULL) {
		WILC_FREE(pstrWFIDrv);
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
	s32 s32Error = WILC_SUCCESS;
	tstrHostIFmsg strHostIFmsg;
	u32 drvHandler;
	tstrWILC_WFIDrv *pstrWFIDrv = NULL;

	drvHandler = ((pu8Buffer[u32Length - 4]) | (pu8Buffer[u32Length - 3] << 8) | (pu8Buffer[u32Length - 2] << 16) | (pu8Buffer[u32Length - 1] << 24));
	pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;




	if (pstrWFIDrv == NULL || pstrWFIDrv == terminated_handle)	{
		PRINT_ER("NetworkInfo received but driver not init[%p]\n", pstrWFIDrv);
		return;
	}

	/* prepare the Asynchronous Network Info message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	strHostIFmsg.u16MsgId = HOST_IF_MSG_RCVD_NTWRK_INFO;
	strHostIFmsg.drvHandler = pstrWFIDrv;

	strHostIFmsg.uniHostIFmsgBody.strRcvdNetworkInfo.u32Length = u32Length;
	strHostIFmsg.uniHostIFmsgBody.strRcvdNetworkInfo.pu8Buffer = (u8 *)WILC_MALLOC(u32Length); /* will be deallocated by the receiving thread */
	WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strRcvdNetworkInfo.pu8Buffer,
		    pu8Buffer, u32Length);

	/* send the message */
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		PRINT_ER("Error in sending network info message queue message parameters: Error(%d)\n", s32Error);
	}


	return;
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
	s32 s32Error = WILC_SUCCESS;
	tstrHostIFmsg strHostIFmsg;
	u32 drvHandler;
	tstrWILC_WFIDrv *pstrWFIDrv = NULL;

	/*BugID_5348*/
	down(&hSemHostIntDeinit);

	drvHandler = ((pu8Buffer[u32Length - 4]) | (pu8Buffer[u32Length - 3] << 8) | (pu8Buffer[u32Length - 2] << 16) | (pu8Buffer[u32Length - 1] << 24));
	pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;
	PRINT_D(HOSTINF_DBG, "General asynchronous info packet received \n");


	if (pstrWFIDrv == NULL || pstrWFIDrv == terminated_handle) {
		PRINT_D(HOSTINF_DBG, "Wifi driver handler is equal to NULL\n");
		/*BugID_5348*/
		up(&hSemHostIntDeinit);
		return;
	}

	if (pstrWFIDrv->strWILC_UsrConnReq.pfUserConnectResult == NULL) {
		/* received mac status is not needed when there is no current Connect Request */
		PRINT_ER("Received mac status is not needed when there is no current Connect Reques\n");
		/*BugID_5348*/
		up(&hSemHostIntDeinit);
		return;
	}

	/* prepare the General Asynchronous Info message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));


	strHostIFmsg.u16MsgId = HOST_IF_MSG_RCVD_GNRL_ASYNC_INFO;
	strHostIFmsg.drvHandler = pstrWFIDrv;


	strHostIFmsg.uniHostIFmsgBody.strRcvdGnrlAsyncInfo.u32Length = u32Length;
	strHostIFmsg.uniHostIFmsgBody.strRcvdGnrlAsyncInfo.pu8Buffer = (u8 *)WILC_MALLOC(u32Length); /* will be deallocated by the receiving thread */
	WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strRcvdGnrlAsyncInfo.pu8Buffer,
		    pu8Buffer, u32Length);

	/* send the message */
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		PRINT_ER("Error in sending message queue asynchronous message info: Error(%d)\n", s32Error);
	}

	/*BugID_5348*/
	up(&hSemHostIntDeinit);
	return;
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
	s32 s32Error = WILC_SUCCESS;
	tstrHostIFmsg strHostIFmsg;
	u32 drvHandler;
	tstrWILC_WFIDrv *pstrWFIDrv = NULL;
	drvHandler = ((pu8Buffer[u32Length - 4]) | (pu8Buffer[u32Length - 3] << 8) | (pu8Buffer[u32Length - 2] << 16) | (pu8Buffer[u32Length - 1] << 24));
	pstrWFIDrv = (tstrWILC_WFIDrv *)drvHandler;


	PRINT_D(GENERIC_DBG, "Scan notification received %p\n", pstrWFIDrv);

	if (pstrWFIDrv == NULL || pstrWFIDrv == terminated_handle) {
		return;
	}

	/*if there is an ongoing scan request*/
	if (pstrWFIDrv->strWILC_UsrScanReq.pfUserScanResult) {
		/* prepare theScan Done message */
		WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

		strHostIFmsg.u16MsgId = HOST_IF_MSG_RCVD_SCAN_COMPLETE;
		strHostIFmsg.drvHandler = pstrWFIDrv;


		/* will be deallocated by the receiving thread */
		/*no need to send message body*/

		/*strHostIFmsg.uniHostIFmsgBody.strScanComplete.u32Length = u32Length;
		 * strHostIFmsg.uniHostIFmsgBody.strScanComplete.pu8Buffer  = (u8*)WILC_MALLOC(u32Length);
		 * WILC_memcpy(strHostIFmsg.uniHostIFmsgBody.strScanComplete.pu8Buffer,
		 *                        pu8Buffer, u32Length); */

		/* send the message */
		s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
		if (s32Error) {
			PRINT_ER("Error in sending message queue scan complete parameters: Error(%d)\n", s32Error);
		}
	}


	return;

}

#ifdef WILC_P2P
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
s32 host_int_remain_on_channel(WILC_WFIDrvHandle hWFIDrv, u32 u32SessionID, u32 u32duration, u16 chan, tWILCpfRemainOnChanExpired RemainOnChanExpired, tWILCpfRemainOnChanReady RemainOnChanReady, void *pvUserArg)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	/* prepare the remainonchan Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_REMAIN_ON_CHAN;
	strHostIFmsg.uniHostIFmsgBody.strHostIfRemainOnChan.u16Channel = chan;
	strHostIFmsg.uniHostIFmsgBody.strHostIfRemainOnChan.pRemainOnChanExpired = RemainOnChanExpired;
	strHostIFmsg.uniHostIFmsgBody.strHostIfRemainOnChan.pRemainOnChanReady = RemainOnChanReady;
	strHostIFmsg.uniHostIFmsgBody.strHostIfRemainOnChan.pVoid = pvUserArg;
	strHostIFmsg.uniHostIFmsgBody.strHostIfRemainOnChan.u32duration = u32duration;
	strHostIFmsg.uniHostIFmsgBody.strHostIfRemainOnChan.u32ListenSessionID = u32SessionID;
	strHostIFmsg.drvHandler = hWFIDrv;

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{

	}

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
s32 host_int_ListenStateExpired(WILC_WFIDrvHandle hWFIDrv, u32 u32SessionID)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	/*Stopping remain-on-channel timer*/
	WILC_TimerStop(&(pstrWFIDrv->hRemainOnChannel), NULL);

	/* prepare the timer fire Message */
	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));
	strHostIFmsg.u16MsgId = HOST_IF_MSG_LISTEN_TIMER_FIRED;
	strHostIFmsg.drvHandler = hWFIDrv;
	strHostIFmsg.uniHostIFmsgBody.strHostIfRemainOnChan.u32ListenSessionID = u32SessionID;

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{

	}
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
s32 host_int_frame_register(WILC_WFIDrvHandle hWFIDrv, u16 u16FrameType, bool bReg)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_REGISTER_FRAME;
	switch (u16FrameType) {
	case ACTION:
		PRINT_D(HOSTINF_DBG, "ACTION\n");
		strHostIFmsg.uniHostIFmsgBody.strHostIfRegisterFrame.u8Regid = ACTION_FRM_IDX;
		break;

	case PROBE_REQ:
		PRINT_D(HOSTINF_DBG, "PROBE REQ\n");
		strHostIFmsg.uniHostIFmsgBody.strHostIfRegisterFrame.u8Regid = PROBE_REQ_IDX;
		break;

	default:
		PRINT_D(HOSTINF_DBG, "Not valid frame type\n");
		break;
	}
	strHostIFmsg.uniHostIFmsgBody.strHostIfRegisterFrame.u16FrameType = u16FrameType;
	strHostIFmsg.uniHostIFmsgBody.strHostIfRegisterFrame.bReg = bReg;
	strHostIFmsg.drvHandler = hWFIDrv;

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{

	}

	return s32Error;


}
#endif

#ifdef WILC_AP_EXTERNAL_MLME
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
s32 host_int_add_beacon(WILC_WFIDrvHandle hWFIDrv, u32 u32Interval,
				u32 u32DTIMPeriod,
				u32 u32HeadLen, u8 *pu8Head,
				u32 u32TailLen, u8 *pu8Tail)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	tstrHostIFSetBeacon *pstrSetBeaconParam = &strHostIFmsg.uniHostIFmsgBody.strHostIFSetBeacon;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	PRINT_D(HOSTINF_DBG, "Setting adding beacon message queue params\n");


	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_ADD_BEACON;
	strHostIFmsg.drvHandler = hWFIDrv;
	pstrSetBeaconParam->u32Interval = u32Interval;
	pstrSetBeaconParam->u32DTIMPeriod = u32DTIMPeriod;
	pstrSetBeaconParam->u32HeadLen = u32HeadLen;
	pstrSetBeaconParam->pu8Head = (u8 *)WILC_MALLOC(u32HeadLen);
	if (pstrSetBeaconParam->pu8Head == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_NO_MEM);
	}
	WILC_memcpy(pstrSetBeaconParam->pu8Head, pu8Head, u32HeadLen);
	pstrSetBeaconParam->u32TailLen = u32TailLen;

	/* Bug 4599 : if tail length = 0 skip allocating & copying */
	if (u32TailLen > 0) {
		pstrSetBeaconParam->pu8Tail = (u8 *)WILC_MALLOC(u32TailLen);
		if (pstrSetBeaconParam->pu8Tail == NULL) {
			WILC_ERRORREPORT(s32Error, WILC_NO_MEM);
		}
		WILC_memcpy(pstrSetBeaconParam->pu8Tail, pu8Tail, u32TailLen);
	} else {
		pstrSetBeaconParam->pu8Tail = NULL;
	}

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}

	WILC_CATCH(s32Error)
	{
		if (pstrSetBeaconParam->pu8Head != NULL) {
			WILC_FREE(pstrSetBeaconParam->pu8Head);
		}

		if (pstrSetBeaconParam->pu8Tail != NULL) {
			WILC_FREE(pstrSetBeaconParam->pu8Tail);
		}
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
s32 host_int_del_beacon(WILC_WFIDrvHandle hWFIDrv)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_DEL_BEACON;
	strHostIFmsg.drvHandler = hWFIDrv;
	PRINT_D(HOSTINF_DBG, "Setting deleting beacon message queue params\n");

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	WILC_ERRORCHECK(s32Error);

	WILC_CATCH(s32Error)
	{
	}
	return s32Error;
}


/**
 *  @brief host_int_add_station
 *  @details       Setting add station params in message queue
 *  @param[in]    WILC_WFIDrvHandle hWFIDrv, tstrWILC_AddStaParam* pstrStaParams
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
s32 host_int_add_station(WILC_WFIDrvHandle hWFIDrv, tstrWILC_AddStaParam *pstrStaParams)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	tstrWILC_AddStaParam *pstrAddStationMsg = &strHostIFmsg.uniHostIFmsgBody.strAddStaParam;


	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	PRINT_D(HOSTINF_DBG, "Setting adding station message queue params\n");


	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_ADD_STATION;
	strHostIFmsg.drvHandler = hWFIDrv;

	WILC_memcpy(pstrAddStationMsg, pstrStaParams, sizeof(tstrWILC_AddStaParam));
	if (pstrAddStationMsg->u8NumRates > 0) {
		u8 *rates = WILC_MALLOC(pstrAddStationMsg->u8NumRates);
		WILC_NULLCHECK(s32Error, rates);

		WILC_memcpy(rates, pstrStaParams->pu8Rates, pstrAddStationMsg->u8NumRates);
		pstrAddStationMsg->pu8Rates = rates;
	}


	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}

	WILC_CATCH(s32Error)
	{
	}
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
s32 host_int_del_station(WILC_WFIDrvHandle hWFIDrv, const u8 *pu8MacAddr)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	tstrHostIFDelSta *pstrDelStationMsg = &strHostIFmsg.uniHostIFmsgBody.strDelStaParam;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	PRINT_D(HOSTINF_DBG, "Setting deleting station message queue params\n");



	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_DEL_STATION;
	strHostIFmsg.drvHandler = hWFIDrv;

	/*BugID_4795: Handling situation of deleting all stations*/
	if (pu8MacAddr == NULL)
		WILC_memset(pstrDelStationMsg->au8MacAddr, 255, ETH_ALEN);
	else
		WILC_memcpy(pstrDelStationMsg->au8MacAddr, pu8MacAddr, ETH_ALEN);

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}

	WILC_CATCH(s32Error)
	{
	}
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
s32 host_int_del_allstation(WILC_WFIDrvHandle hWFIDrv, u8 pu8MacAddr[][ETH_ALEN])
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	tstrHostIFDelAllSta *pstrDelAllStationMsg = &strHostIFmsg.uniHostIFmsgBody.strHostIFDelAllSta;
	u8 au8Zero_Buff[ETH_ALEN] = {0};
	u32 i;
	u8 u8AssocNumb = 0;


	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	PRINT_D(HOSTINF_DBG, "Setting deauthenticating station message queue params\n");

	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_DEL_ALL_STA;
	strHostIFmsg.drvHandler = hWFIDrv;

	/* Handling situation of deauthenticing all associated stations*/
	for (i = 0; i < MAX_NUM_STA; i++) {
		if (memcmp(pu8MacAddr[i], au8Zero_Buff, ETH_ALEN)) {
			WILC_memcpy(pstrDelAllStationMsg->au8Sta_DelAllSta[i], pu8MacAddr[i], ETH_ALEN);
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
	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);


	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);

	}
	WILC_CATCH(s32Error)
	{

	}
	down(&hWaitResponse);

	return s32Error;

}

/**
 *  @brief host_int_edit_station
 *  @details       Setting edit station params in message queue
 *  @param[in]    WILC_WFIDrvHandle hWFIDrv, tstrWILC_AddStaParam* pstrStaParams
 *  @return         Error code.
 *  @author
 *  @date
 *  @version	1.0
 */
s32 host_int_edit_station(WILC_WFIDrvHandle hWFIDrv, tstrWILC_AddStaParam *pstrStaParams)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	tstrWILC_AddStaParam *pstrAddStationMsg = &strHostIFmsg.uniHostIFmsgBody.strAddStaParam;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	PRINT_D(HOSTINF_DBG, "Setting editing station message queue params\n");

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));


	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_EDIT_STATION;
	strHostIFmsg.drvHandler = hWFIDrv;

	WILC_memcpy(pstrAddStationMsg, pstrStaParams, sizeof(tstrWILC_AddStaParam));
	if (pstrAddStationMsg->u8NumRates > 0) {
		u8 *rates = WILC_MALLOC(pstrAddStationMsg->u8NumRates);
		WILC_NULLCHECK(s32Error, rates);
		WILC_memcpy(rates, pstrStaParams->pu8Rates, pstrAddStationMsg->u8NumRates);
		pstrAddStationMsg->pu8Rates = rates;
	}

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{
	}
	return s32Error;
}
#endif /*WILC_AP_EXTERNAL_MLME*/
uint32_t wilc_get_chipid(uint8_t);

s32 host_int_set_power_mgmt(WILC_WFIDrvHandle hWFIDrv, bool bIsEnabled, u32 u32Timeout)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	tstrHostIfPowerMgmtParam *pstrPowerMgmtParam = &strHostIFmsg.uniHostIFmsgBody.strPowerMgmtparam;

	PRINT_INFO(HOSTINF_DBG, "\n\n>> Setting PS to %d << \n\n", bIsEnabled);

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	PRINT_D(HOSTINF_DBG, "Setting Power management message queue params\n");

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));


	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_POWER_MGMT;
	strHostIFmsg.drvHandler = hWFIDrv;

	pstrPowerMgmtParam->bIsEnabled = bIsEnabled;
	pstrPowerMgmtParam->u32Timeout = u32Timeout;


	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{
	}
	return s32Error;
}

s32 host_int_setup_multicast_filter(WILC_WFIDrvHandle hWFIDrv, bool bIsEnabled, u32 u32count)
{
	s32 s32Error = WILC_SUCCESS;

	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	tstrHostIFSetMulti *pstrMulticastFilterParam = &strHostIFmsg.uniHostIFmsgBody.strHostIfSetMulti;


	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	PRINT_D(HOSTINF_DBG, "Setting Multicast Filter params\n");

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));


	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_SET_MULTICAST_FILTER;
	strHostIFmsg.drvHandler = hWFIDrv;

	pstrMulticastFilterParam->bIsEnabled = bIsEnabled;
	pstrMulticastFilterParam->u32count = u32count;

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{
	}
	return s32Error;
}



/*Bug4218: Parsing Join Param*/
#ifdef WILC_PARSE_SCAN_IN_HOST

/*Bug4218: Parsing Join Param*/
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

	pNewJoinBssParam = WILC_MALLOC(sizeof(tstrJoinBssParam));
	if (pNewJoinBssParam != NULL) {
		WILC_memset(pNewJoinBssParam, 0, sizeof(tstrJoinBssParam));
		pNewJoinBssParam->dtim_period = ptstrNetworkInfo->u8DtimPeriod;
		pNewJoinBssParam->beacon_period = ptstrNetworkInfo->u16BeaconPeriod;
		pNewJoinBssParam->cap_info = ptstrNetworkInfo->u16CapInfo;
		WILC_memcpy(pNewJoinBssParam->au8bssid, ptstrNetworkInfo->au8bssid, 6);
		/*for(i=0; i<6;i++)
		 *      PRINT_D(HOSTINF_DBG,"%c",pNewJoinBssParam->au8bssid[i]);*/
		WILC_memcpy((u8 *)pNewJoinBssParam->ssid, ptstrNetworkInfo->au8ssid, ptstrNetworkInfo->u8SsidLen + 1);
		pNewJoinBssParam->ssidLen = ptstrNetworkInfo->u8SsidLen;
		WILC_memset(pNewJoinBssParam->rsn_pcip_policy, 0xFF, 3);
		WILC_memset(pNewJoinBssParam->rsn_auth_policy, 0xFF, 3);
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
				if (pu8IEs[index + 8] & (1 << 7)) {
					pNewJoinBssParam->uapsd_cap = true;
				}
				index += pu8IEs[index + 1] + 2;
				continue;
			}
			#ifdef WILC_P2P
			else if ((pu8IEs[index] == P2P_IE) && /* P2P Element ID */
				 (pu8IEs[index + 2] == 0x50) && (pu8IEs[index + 3] == 0x6f) &&
				 (pu8IEs[index + 4] == 0x9a) && /* OUI */
				 (pu8IEs[index + 5] == 0x09) && (pu8IEs[index + 6] == 0x0c)) { /* OUI Type     */
				u16 u16P2P_count;
				pNewJoinBssParam->tsf = ptstrNetworkInfo->u32Tsf;
				pNewJoinBssParam->u8NoaEnbaled = 1;
				pNewJoinBssParam->u8Index = pu8IEs[index + 9];

				/* Check if Bit 7 is set indicating Opss capability */
				if (pu8IEs[index + 10] & (1 << 7)) {
					pNewJoinBssParam->u8OppEnable = 1;
					pNewJoinBssParam->u8CtWindow = pu8IEs[index + 10];
				} else
					pNewJoinBssParam->u8OppEnable = 0;
				/* HOSTINF_DBG */
				PRINT_D(GENERIC_DBG, "P2P Dump \n");
				for (i = 0; i < pu8IEs[index + 7]; i++)
					PRINT_D(GENERIC_DBG, " %x \n", pu8IEs[index + 9 + i]);

				pNewJoinBssParam->u8Count = pu8IEs[index + 11];
				u16P2P_count = index + 12;

				WILC_memcpy(pNewJoinBssParam->au8Duration, pu8IEs + u16P2P_count, 4);
				u16P2P_count += 4;

				WILC_memcpy(pNewJoinBssParam->au8Interval, pu8IEs + u16P2P_count, 4);
				u16P2P_count += 4;

				WILC_memcpy(pNewJoinBssParam->au8StartTime, pu8IEs + u16P2P_count, 4);

				index += pu8IEs[index + 1] + 2;
				continue;

			}
			#endif
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
				/* PRINT_D(HOSTINF_DBG,"Group Policy: %0x \n",pNewJoinBssParam->rsn_grp_policy); */
				/* initialize policies with invalid values */

				jumpOffset = pu8IEs[rsnIndex] * 4; /* total no.of bytes of pcipher field (count*4) */

				/*parsing pairwise cipher*/

				/* saving 3 pcipher max. */
				pcipherCount = (pu8IEs[rsnIndex] > 3) ? 3 : pu8IEs[rsnIndex];
				rsnIndex += 2; /* jump 2 bytes of pcipher count */

				/* PRINT_D(HOSTINF_DBG,"\npcipher:%d \n",pcipherCount); */
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
		WILC_FREE((tstrJoinBssParam *)pJoinParams);
	else
		PRINT_ER("Unable to FREE null pointer\n");
}
#endif  /*WILC_PARSE_SCAN_IN_HOST*/


/**
 *  @brief              host_int_addBASession
 *  @details            Open a block Ack session with the given parameters
 *  @param[in]          tstrNetworkInfo* ptstrNetworkInfo
 *  @return
 *  @author		anoureldin
 *  @date
 *  @version		1.0**/

static int host_int_addBASession(WILC_WFIDrvHandle hWFIDrv, char *pBSSID, char TID, short int BufferSize,
				 short int SessionTimeout, void *drvHandler)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	tstrHostIfBASessionInfo *pBASessionInfo = &strHostIFmsg.uniHostIFmsgBody.strHostIfBASessionInfo;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_ADD_BA_SESSION;

	memcpy(pBASessionInfo->au8Bssid, pBSSID, ETH_ALEN);
	pBASessionInfo->u8Ted = TID;
	pBASessionInfo->u16BufferSize = BufferSize;
	pBASessionInfo->u16SessionTimeout = SessionTimeout;
	strHostIFmsg.drvHandler = hWFIDrv;

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{

	}

	return s32Error;
}


s32 host_int_delBASession(WILC_WFIDrvHandle hWFIDrv, char *pBSSID, char TID)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	tstrHostIfBASessionInfo *pBASessionInfo = &strHostIFmsg.uniHostIFmsgBody.strHostIfBASessionInfo;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_DEL_BA_SESSION;

	memcpy(pBASessionInfo->au8Bssid, pBSSID, ETH_ALEN);
	pBASessionInfo->u8Ted = TID;
	strHostIFmsg.drvHandler = hWFIDrv;

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{

	}

	/*BugID_5222*/
	down(&hWaitResponse);

	return s32Error;
}

s32 host_int_del_All_Rx_BASession(WILC_WFIDrvHandle hWFIDrv, char *pBSSID, char TID)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;
	tstrHostIfBASessionInfo *pBASessionInfo = &strHostIFmsg.uniHostIFmsgBody.strHostIfBASessionInfo;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_DEL_ALL_RX_BA_SESSIONS;

	memcpy(pBASessionInfo->au8Bssid, pBSSID, ETH_ALEN);
	pBASessionInfo->u8Ted = TID;
	strHostIFmsg.drvHandler = hWFIDrv;

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{

	}

	/*BugID_5222*/
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
s32 host_int_setup_ipaddress(WILC_WFIDrvHandle hWFIDrv, u8 *u16ipadd, u8 idx)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;

	/* TODO: Enable This feature on softap firmware */
	return 0;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_SET_IPADDRESS;

	strHostIFmsg.uniHostIFmsgBody.strHostIfSetIP.au8IPAddr = u16ipadd;
	strHostIFmsg.drvHandler = hWFIDrv;
	strHostIFmsg.uniHostIFmsgBody.strHostIfSetIP.idx = idx;

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{

	}

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
s32 host_int_get_ipaddress(WILC_WFIDrvHandle hWFIDrv, u8 *u16ipadd, u8 idx)
{
	s32 s32Error = WILC_SUCCESS;
	tstrWILC_WFIDrv *pstrWFIDrv = (tstrWILC_WFIDrv *)hWFIDrv;
	tstrHostIFmsg strHostIFmsg;

	if (pstrWFIDrv == NULL) {
		WILC_ERRORREPORT(s32Error, WILC_INVALID_ARGUMENT);
	}

	WILC_memset(&strHostIFmsg, 0, sizeof(tstrHostIFmsg));

	/* prepare the WiphyParams Message */
	strHostIFmsg.u16MsgId = HOST_IF_MSG_GET_IPADDRESS;

	strHostIFmsg.uniHostIFmsgBody.strHostIfSetIP.au8IPAddr = u16ipadd;
	strHostIFmsg.drvHandler=hWFIDrv;
	strHostIFmsg.uniHostIFmsgBody.strHostIfSetIP.idx= idx;

	s32Error = WILC_MsgQueueSend(&gMsgQHostIF, &strHostIFmsg, sizeof(tstrHostIFmsg), NULL);
	if (s32Error) {
		WILC_ERRORREPORT(s32Error, s32Error);
	}
	WILC_CATCH(s32Error)
	{

	}

	return s32Error;


}
