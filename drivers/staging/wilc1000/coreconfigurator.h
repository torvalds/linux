
/*!
 *  @file	coreconfigurator.h
 *  @brief
 *  @author
 *  @sa		coreconfigurator.c
 *  @date	1 Mar 2012
 *  @version	1.0
 */


#ifndef CORECONFIGURATOR_H
#define CORECONFIGURATOR_H

#include "wilc_oswrapper.h"
#include "wilc_wlan_if.h"
/*****************************************************************************/
/* Constants                                                                 */
/*****************************************************************************/
/* Number of WID Options Supported */
#define NUM_BASIC_SWITCHES      45
#define NUM_FHSS_SWITCHES        0

#define NUM_RSSI	5

#ifdef MAC_802_11N
#define NUM_11N_BASIC_SWITCHES  25
#define NUM_11N_HUT_SWITCHES    47
#else /* MAC_802_11N */
#define NUM_11N_BASIC_SWITCHES  0
#define NUM_11N_HUT_SWITCHES    0
#endif /* MAC_802_11N */

extern WILC_Uint16 g_num_total_switches;

#define MAC_HDR_LEN             24          /* No Address4 - non-ESS         */
#define MAX_SSID_LEN            33
#define FCS_LEN                 4
#define TIME_STAMP_LEN          8
#define BEACON_INTERVAL_LEN     2
#define CAP_INFO_LEN            2
#define STATUS_CODE_LEN         2
#define AID_LEN                 2
#define IE_HDR_LEN              2


/* Operating Mode: SET */
#define SET_CFG              0
/* Operating Mode: GET */
#define GET_CFG              1

#define MAX_PACKET_BUFF_SIZE 1596

#define MAX_STRING_LEN					256
#define MAX_SURVEY_RESULT_FRAG_SIZE MAX_STRING_LEN
#define SURVEY_RESULT_LENGTH		44
#define MAX_ASSOC_RESP_FRAME_SIZE MAX_STRING_LEN

#define STATUS_MSG_LEN			12
#define MAC_CONNECTED			1
#define MAC_DISCONNECTED		0



/*****************************************************************************/
/* Function Macros                                                           */
/*****************************************************************************/
#define MAKE_WORD16(lsb, msb) ((((WILC_Uint16)(msb) << 8) & 0xFF00) | (lsb))
#define MAKE_WORD32(lsw, msw) ((((WILC_Uint32)(msw) << 16) & 0xFFFF0000) | (lsw))


/*****************************************************************************/
/* Type Definitions                                                                                                                       */
/*****************************************************************************/
/* WID Data Types */
#if 0
typedef enum {
	WID_CHAR  = 0,
	WID_SHORT = 1,
	WID_INT   = 2,
	WID_STR   = 3,
	WID_ADR   = 4,
	WID_BIN   = 5,
	WID_IP    = 6,
	WID_UNDEF = 7,
	WID_TYPE_FORCE_32BIT  = 0xFFFFFFFF
} tenuWIDtype;

/* WLAN Identifiers */
typedef enum {
	WID_NIL                            = -1,
	/* EMAC Character WID list */
	WID_BSS_TYPE                       = 0x0000,
	WID_CURRENT_TX_RATE                = 0x0001,
	WID_CURRENT_CHANNEL                = 0x0002,
	WID_PREAMBLE                       = 0x0003,
	WID_11G_OPERATING_MODE             = 0x0004,
	WID_STATUS                         = 0x0005,
	WID_11G_PROT_MECH                  = 0x0006,
	WID_SCAN_TYPE                      = 0x0007,
	WID_PRIVACY_INVOKED                = 0x0008,
	WID_KEY_ID                         = 0x0009,
	WID_QOS_ENABLE                     = 0x000A,
	WID_POWER_MANAGEMENT               = 0x000B,
	WID_11I_MODE                       = 0x000C,
	WID_AUTH_TYPE                      = 0x000D,
	WID_SITE_SURVEY                    = 0x000E,
	WID_LISTEN_INTERVAL                = 0x000F,
	WID_DTIM_PERIOD                    = 0x0010,
	WID_ACK_POLICY                     = 0x0011,
	WID_RESET                          = 0x0012,
	WID_PCF_MODE                       = 0x0013,
	WID_CFP_PERIOD                     = 0x0014,
	WID_BCAST_SSID                     = 0x0015,
	WID_PHY_TEST_PATTERN               = 0x0016,
	WID_DISCONNECT                     = 0x0016,
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
	WID_DEVICE_READY                   = 0x003D,
	WID_PM_NULL_FRAME_INTERVAL         = 0x003E,
	WID_PM_ACTIVITY_TIMER              = 0x003F,
	WID_PM_NULL_FRAME_WAIT_ENABLE      = 0x0040,
	WID_SCAN_WAIT_TIME                 = 0x0041,
	WID_WSC_IE_EN                      = 0x0042,
	WID_WPS_START                      = 0x0043,
	WID_WPS_DEV_MODE                   = 0x0044,
	WID_BT_COEXISTENCE                 = 0x0050,
	WID_TRACKING_ROAMING               = 0x0070,
	WID_NUM_PKTS_FOR_RSSI_AVG          = 0x0071,
	WID_FHSS_SCAN_CHAN_INDEX           = 0x0072,
	WID_FHSS_SCAN_STEP_INDEX           = 0x0073,

	/* NMAC Character WID list */
	WID_11N_PROT_MECH                  = 0x0080,
	WID_11N_ERP_PROT_TYPE              = 0x0081,
	WID_11N_ENABLE                     = 0x0082,
	WID_11N_OPERATING_MODE             = 0x0083,
	WID_11N_OBSS_NONHT_DETECTION       = 0x0084,
	WID_11N_HT_PROT_TYPE               = 0x0085,
	WID_11N_RIFS_PROT_ENABLE           = 0x0086,
	WID_11N_SMPS_MODE                  = 0x0087,
	WID_11N_CURRENT_TX_MCS             = 0x0088,
	WID_11N_PRINT_STATS                = 0x0089,
	WID_HUT_FCS_CORRUPT_MODE           = 0x008A,
	WID_HUT_RESTART                    = 0x008B,
	WID_HUT_TX_FORMAT                  = 0x008C,
	WID_11N_SHORT_GI_20MHZ_ENABLE      = 0x008D,
	WID_HUT_BANDWIDTH                  = 0x008E,
	WID_HUT_OP_BAND                    = 0x008F,
	WID_HUT_STBC                       = 0x0090,
	WID_HUT_ESS                        = 0x0091,
	WID_HUT_ANTSET                     = 0x0092,
	WID_HUT_HT_OP_MODE                 = 0x0093,
	WID_HUT_RIFS_MODE                  = 0x0094,
	WID_HUT_SMOOTHING_REC              = 0x0095,
	WID_HUT_SOUNDING_PKT               = 0x0096,
	WID_HUT_HT_CODING                  = 0x0097,
	WID_HUT_TEST_DIR                   = 0x0098,
	WID_HUT_CAPTURE_MODE               = 0x0099,
	WID_HUT_PHY_TEST_MODE              = 0x009A,
	WID_HUT_PHY_TEST_RATE_HI           = 0x009B,
	WID_HUT_PHY_TEST_RATE_LO           = 0x009C,
	WID_HUT_DISABLE_RXQ_REPLENISH      = 0x009D,
	WID_HUT_KEY_ORIGIN                 = 0x009E,
	WID_HUT_BCST_PERCENT               = 0x009F,
	WID_HUT_GROUP_CIPHER_TYPE          = 0x00A0,
	WID_TX_ABORT_CONFIG                = 0x00A1,
	WID_HOST_DATA_IF_TYPE              = 0x00A2,
	WID_HOST_CONFIG_IF_TYPE            = 0x00A3,
	WID_HUT_TSF_TEST_MODE              = 0x00A4,
	WID_HUT_TSSI_VALUE                 = 0x00A5,
	WID_HUT_PKT_TSSI_VALUE             = 0x00A5,
	WID_REG_TSSI_11B_VALUE             = 0x00A6,
	WID_REG_TSSI_11G_VALUE             = 0x00A7,
	WID_REG_TSSI_11N_VALUE             = 0x00A8,
	WID_TX_CALIBRATION                 = 0x00A9,
	WID_DSCR_TSSI_11B_VALUE            = 0x00AA,
	WID_DSCR_TSSI_11G_VALUE            = 0x00AB,
	WID_DSCR_TSSI_11N_VALUE            = 0x00AC,
	WID_HUT_RSSI_EX                    = 0x00AD,
	WID_HUT_ADJ_RSSI_EX                = 0x00AE,
	WID_11N_IMMEDIATE_BA_ENABLED       = 0x00AF,
	WID_11N_TXOP_PROT_DISABLE          = 0x00B0,
	WID_TX_POWER_LEVEL_11N             = 0x00B1,
	WID_HUT_MGMT_PERCENT               = 0x00B3,
	WID_HUT_MGMT_BCST_PERCENT          = 0x00B4,
	WID_HUT_MGMT_ALLOW_HT              = 0x00B5,
	WID_HUT_UC_MGMT_TYPE               = 0x00B6,
	WID_HUT_BC_MGMT_TYPE               = 0x00B7,
	WID_HUT_11W_MFP_REQUIRED_TX        = 0x00B8,
	WID_HUT_11W_MFP_PEER_CAPABLE       = 0x00B9,
	WID_HUT_11W_TX_IGTK_ID             = 0x00BA,
	WID_11W_ENABLE                     = 0x00BB,
	WID_11W_MGMT_PROT_REQ              = 0x00BC,
	WID_USER_SEC_CHANNEL_OFFSET        = 0x00C0,
	WID_2040_COEXISTENCE               = 0x00C1,
	WID_HUT_FC_TXOP_MOD                = 0x00C2,
	WID_HUT_FC_PROT_TYPE               = 0x00C3,
	WID_HUT_SEC_CCA_ASSERT             = 0x00C4,
	WID_2040_ENABLE                    = 0x00C5,
	WID_2040_CURR_CHANNEL_OFFSET       = 0x00C6,
	WID_2040_40MHZ_INTOLERANT          = 0x00C7,


	/* Custom Character WID list */
	WID_POWER_SAVE                     = 0x0100,
	WID_WAKE_STATUS                    = 0x0101,
	WID_WAKE_CONTROL                   = 0x0102,
	WID_CCA_BUSY_START                 = 0x0103,

	/* EMAC Short WID list */
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
	WID_JOIN_START_TIMEOUT             = 0x100F,
	WID_AUTH_TIMEOUT                   = 0x1010,
	WID_ASOC_TIMEOUT                   = 0x1011,
	WID_11I_PROTOCOL_TIMEOUT           = 0x1012,
	WID_EAPOL_RESPONSE_TIMEOUT         = 0x1013,
	WID_WPS_PASS_ID                    = 0x1017,
	WID_WPS_CONFIG_METHOD              = 0x1018,
	WID_FHSS_INIT_SCAN_TIME            = 0x1070,
	WID_FHSS_ROAM_SCAN_TIME            = 0x1071,

	/* NMAC Short WID list */
	WID_11N_RF_REG_VAL                 = 0x1080,
	WID_HUT_FRAME_LEN                  = 0x1081,
	WID_HUT_TXOP_LIMIT                 = 0x1082,
	WID_HUT_SIG_QUAL_AVG               = 0x1083,
	WID_HUT_SIG_QUAL_AVG_CNT           = 0x1084,
	WID_11N_SIG_QUAL_VAL               = 0x1085,
	WID_HUT_RSSI_EX_COUNT              = 0x1086,
	WID_HUT_UC_MGMT_FRAME_LEN          = 0x1088,
	WID_HUT_BC_MGMT_FRAME_LEN          = 0x1089,

	/* Custom Short WID list */

	WID_CCA_BUSY_STATUS                = 0x1100,

	/* EMAC Integer WID list */
	WID_FAILED_COUNT                   = 0x2000,
	WID_RETRY_COUNT                    = 0x2001,
	WID_MULTIPLE_RETRY_COUNT           = 0x2002,
	WID_FRAME_DUPLICATE_COUNT          = 0x2003,
	WID_ACK_FAILURE_COUNT              = 0x2004,
	WID_RECEIVED_FRAGMENT_COUNT        = 0x2005,
	WID_MCAST_RECEIVED_FRAME_COUNT     = 0x2006,
	WID_FCS_ERROR_COUNT                = 0x2007,
	WID_SUCCESS_FRAME_COUNT            = 0x2008,
	WID_PHY_TEST_PKT_CNT               = 0x2009,
	WID_HUT_TX_COUNT                   = 0x200A,
	WID_TX_FRAGMENT_COUNT              = 0x200B,
	WID_TX_MULTICAST_FRAME_COUNT       = 0x200C,
	WID_RTS_SUCCESS_COUNT              = 0x200D,
	WID_RTS_FAILURE_COUNT              = 0x200E,
	WID_WEP_UNDECRYPTABLE_COUNT        = 0x200F,
	WID_REKEY_PERIOD                   = 0x2010,
	WID_REKEY_PACKET_COUNT             = 0x2011,
	WID_1X_SERV_ADDR                   = 0x2012,
	WID_STACK_IP_ADDR                  = 0x2013,
	WID_STACK_NETMASK_ADDR             = 0x2014,
	WID_HW_RX_COUNT                    = 0x2015,
	WID_MEMORY_ADDRESS                 = 0x201E,
	WID_MEMORY_ACCESS_32BIT            = 0x201F,
	WID_RF_REG_VAL                     = 0x2021,
	WID_FIRMWARE_INFO                  = 0x2023,
	WID_DEV_OS_VERSION                 = 0x2025,
	WID_ROAM_RSSI_THESHOLDS            = 0x2070,
	WID_TRACK_INTERVAL_SEC             = 0x2071,
	WID_FHSS_HOPPING_PARAMS            = 0x2072,
	WID_FHSS_HOP_DWELL_TIME            = 0x2073,

	/* NMAC Integer WID list */
	WID_11N_PHY_ACTIVE_REG_VAL         = 0x2080,
	WID_HUT_NUM_TX_PKTS                = 0x2081,
	WID_HUT_TX_TIME_TAKEN              = 0x2082,
	WID_HUT_TX_TEST_TIME               = 0x2083,
	WID_HUT_LOG_INTERVAL               = 0x2084,

	/* EMAC String WID list */
	WID_SSID                           = 0x3000,
	WID_FIRMWARE_VERSION               = 0x3001,
	WID_OPERATIONAL_RATE_SET           = 0x3002,
	WID_BSSID                          = 0x3003,
	#if 0
	WID_WEP_KEY_VALUE0                 = 0x3004,
	#endif
	WID_11I_PSK                        = 0x3008,
	WID_11E_P_ACTION_REQ               = 0x3009,
	WID_1X_KEY                         = 0x300A,
	WID_HARDWARE_VERSION               = 0x300B,
	WID_MAC_ADDR                       = 0x300C,
	WID_HUT_DEST_ADDR                  = 0x300D,
	/*WID_HUT_STATS                      = 0x300E,*/
	WID_PHY_VERSION                    = 0x300F,
	WID_SUPP_USERNAME                  = 0x3010,
	WID_SUPP_PASSWORD                  = 0x3011,
	WID_SITE_SURVEY_RESULTS            = 0x3012,
	WID_RX_POWER_LEVEL                 = 0x3013,
	WID_MANUFACTURER                   = 0x3026,             /*Added for CAPI tool */
	WID_MODEL_NAME                                         = 0x3027,             /*Added for CAPI tool */
	WID_MODEL_NUM                      = 0x3028,             /*Added for CAPI tool */
	WID_DEVICE_NAME                                        = 0x3029,             /*Added for CAPI tool */

	WID_ASSOC_RES_INFO                 = 0x3020,

	/* NMAC String WID list */
	WID_11N_P_ACTION_REQ               = 0x3080,
	WID_HUT_TEST_ID                    = 0x3081,
	WID_PMKID_INFO                     = 0x3082,

	/* Custom String WID list */
	WID_FLASH_DATA                     = 0x3100,
	WID_EEPROM_DATA                    = 0x3101,
	WID_SERIAL_NUMBER                  = 0x3102,

	/* EMAC Binary WID list */
	WID_UAPSD_CONFIG                   = 0x4001,
	WID_UAPSD_STATUS                   = 0x4002,
	WID_AC_PARAMS_AP                   = 0x4003,
	WID_AC_PARAMS_STA                  = 0x4004,
	WID_NEWORK_INFO                    = 0x4005,
	WID_WPS_CRED_LIST                  = 0x4006,
	WID_PRIM_DEV_TYPE                  = 0x4007,
	WID_STA_JOIN_INFO                  = 0x4008,
	WID_CONNECTED_STA_LIST             = 0x4009,

	/* NMAC Binary WID list */
	WID_11N_AUTORATE_TABLE             = 0x4080,
	WID_HUT_TX_PATTERN                 = 0x4081,
	WID_HUT_STATS                      = 0x4082,
	WID_HUT_LOG_STATS                  = 0x4083,

	/*BugID_3746 WID to add IE to be added in next probe request*/
	WID_INFO_ELEMENT_PROBE	= 0x4085,
	/*BugID_3746 WID to add IE to be added in next associate request*/
	WID_INFO_ELEMENT_ASSOCIATE	= 0x4086,

	/* Miscellaneous WIDs */
	WID_ALL                            = 0x7FFE,
	WID_MAX                            = 0xFFFF
} tenuWIDid;
#endif

/* Status Codes for Authentication and Association Frames */
typedef enum {
	SUCCESSFUL_STATUSCODE	= 0,
	UNSPEC_FAIL				  = 1,
	UNSUP_CAP				  = 10,
	REASOC_NO_ASOC				  = 11,
	FAIL_OTHER				  = 12,
	UNSUPT_ALG				  = 13,
	AUTH_SEQ_FAIL			  = 14,
	CHLNG_FAIL				  = 15,
	AUTH_TIMEOUT			  = 16,
	AP_FULL					  = 17,
	UNSUP_RATE				  = 18,
	SHORT_PREAMBLE_UNSUP	  = 19,
	PBCC_UNSUP				  = 20,
	CHANNEL_AGIL_UNSUP	  = 21,
	SHORT_SLOT_UNSUP		  = 25,
	OFDM_DSSS_UNSUP			  = 26,
	CONNECT_STS_FORCE_16_BIT = 0xFFFF
} tenuConnectSts;

typedef struct {
	WILC_Uint16 u16WIDid;
	tenuWIDtype enuWIDtype;
	WILC_Sint32 s32ValueSize;
	WILC_Sint8      *ps8WidVal;

} tstrWID;

typedef struct {
	WILC_Uint8 u8Full;
	WILC_Uint8 u8Index;
	WILC_Sint8 as8RSSI[NUM_RSSI];
} tstrRSSI;
/* This structure is used to support parsing of the received 'N' message */
typedef struct {
	WILC_Sint8 s8rssi;
	WILC_Uint16 u16CapInfo;
	WILC_Uint8 au8ssid[MAX_SSID_LEN];
	WILC_Uint8 u8SsidLen;
	WILC_Uint8 au8bssid[6];
	WILC_Uint16 u16BeaconPeriod;
	WILC_Uint8 u8DtimPeriod;
	WILC_Uint8 u8channel;
	unsigned long u32TimeRcvdInScanCached; /* of type unsigned long to be accepted by the linux kernel macro time_after() */
	unsigned long u32TimeRcvdInScan;
	WILC_Bool bNewNetwork;
#ifdef AGING_ALG
	WILC_Uint8 u8Found;
#endif
#ifdef WILC_P2P
	WILC_Uint32 u32Tsf; /* time-stamp [Low only 32 bit] */
#endif
	WILC_Uint8 *pu8IEs;
	WILC_Uint16 u16IEsLen;
	void *pJoinParams;
	tstrRSSI strRssi;
	WILC_Uint64 u64Tsf; /* time-stamp [Low and High 64 bit] */
} tstrNetworkInfo;

/* This structure is used to support parsing of the received Association Response frame */
typedef struct {
	WILC_Uint16 u16capability;
	WILC_Uint16 u16ConnectStatus;
	WILC_Uint16 u16AssocID;
	WILC_Uint8 *pu8RespIEs;
	WILC_Uint16 u16RespIEsLen;
} tstrConnectRespInfo;


typedef struct {
	WILC_Uint8 au8bssid[6];
	WILC_Uint8 *pu8ReqIEs;
	size_t ReqIEsLen;
	WILC_Uint8 *pu8RespIEs;
	WILC_Uint16 u16RespIEsLen;
	WILC_Uint16 u16ConnectStatus;
} tstrConnectInfo;



typedef struct {
	WILC_Uint16 u16reason;
	WILC_Uint8 *ie;
	size_t ie_len;
} tstrDisconnectNotifInfo;

#ifndef CONNECT_DIRECT
typedef struct wid_site_survey_reslts {
	WILC_Char SSID[MAX_SSID_LEN];
	WILC_Uint8 BssType;
	WILC_Uint8 Channel;
	WILC_Uint8 SecurityStatus;
	WILC_Uint8 BSSID[6];
	WILC_Char RxPower;
	WILC_Uint8 Reserved;

} wid_site_survey_reslts_s;
#endif

extern WILC_Sint32 CoreConfiguratorInit(void);
extern WILC_Sint32 CoreConfiguratorDeInit(void);

extern WILC_Sint32 SendConfigPkt(WILC_Uint8 u8Mode, tstrWID *pstrWIDs,
				 WILC_Uint32 u32WIDsCount, WILC_Bool bRespRequired, WILC_Uint32 drvHandler);
extern WILC_Sint32 ParseNetworkInfo(WILC_Uint8 *pu8MsgBuffer, tstrNetworkInfo **ppstrNetworkInfo);
extern WILC_Sint32 DeallocateNetworkInfo(tstrNetworkInfo *pstrNetworkInfo);

extern WILC_Sint32 ParseAssocRespInfo(WILC_Uint8 *pu8Buffer, WILC_Uint32 u32BufferLen,
				      tstrConnectRespInfo **ppstrConnectRespInfo);
extern WILC_Sint32 DeallocateAssocRespInfo(tstrConnectRespInfo *pstrConnectRespInfo);

#ifndef CONNECT_DIRECT
extern WILC_Sint32 ParseSurveyResults(WILC_Uint8 ppu8RcvdSiteSurveyResults[][MAX_SURVEY_RESULT_FRAG_SIZE],
				      wid_site_survey_reslts_s **ppstrSurveyResults, WILC_Uint32 *pu32SurveyResultsCount);
extern WILC_Sint32 DeallocateSurveyResults(wid_site_survey_reslts_s *pstrSurveyResults);
#endif

extern WILC_Sint32 SendRawPacket(WILC_Sint8 *pspacket, WILC_Sint32 s32PacketLen);
extern void NetworkInfoReceived(WILC_Uint8 *pu8Buffer, WILC_Uint32 u32Length);
void GnrlAsyncInfoReceived(WILC_Uint8 *pu8Buffer, WILC_Uint32 u32Length);
void host_int_ScanCompleteReceived(WILC_Uint8 *pu8Buffer, WILC_Uint32 u32Length);

#endif
