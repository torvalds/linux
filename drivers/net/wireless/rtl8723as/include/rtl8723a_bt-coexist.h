/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 *
 ******************************************************************************/
#ifndef __RTL8723A_BT_COEXIST_H__
#define __RTL8723A_BT_COEXIST_H__

#include <drv_types.h>
#include "../hal/OUTSRC/odm_precomp.h"


#define __BT_C__ 1
#define __BT_HANDLEPACKET_C__ 1
#define __BT_HCI_C__ 1
#define __HALBTC87231ANT_C__ 1
#define __HALBTC87232ANT_C__ 1
#define __HALBTC8723_C__ 1
#define __HALBTCCSR1ANT_C__ 1
#define __HALBTCCSR2ANT_C__ 1
#define __HALBTCOEXIST_C__ 1
#define __HALBT_C__ 1

#ifdef __BT_C__ // COMMON/BT.h
// ===== Below this line is sync from SD7 driver COMMON/BT.h =====

#define	BT_TMP_BUF_SIZE		100

void BT_WifiScanNotify(PADAPTER padapter, u8 scanType);
void BT_WifiAssociateNotify(PADAPTER padapter, u8 action);

// ===== End of sync from SD7 driver COMMON/BT.h =====
#endif // __BT_C__

#ifdef __BT_HCI_C__ // COMMON/bt_hci.h

// HEADER/SecurityType.h
#define TKIP_ENC_KEY_POS		32		//(KEK_LEN+KEK_LEN)
#define MAXRSNIELEN				256

// HEADER/QoSType.h
#if 0
//
// BSS QOS data.
// Ref: BssDscr in 8185 code. [def. in BssDscr.h]
//
typedef struct _BSS_QOS
{
	// Part 0. Ref. 8185 QoS code (From Emily)
	QOS_MODE				bdQoSMode;
	u8					bdWMMIEBuf[MAX_WMMELE_LENGTH];
	OCTET_STRING			bdWMMIE;

	QOS_ELE_SUBTYPE		EleSubType;

	// Part 2. EDCA Parameter (perAC)
	u8					*pWMMInfoEle;
	u8					*pWMMParamEle;

	// QBSS Load.
	u8					QBssLoad[QBSS_LOAD_SIZE];
	u8					bQBssLoadValid;
} BSS_QOS, *PBSS_QOS;
#endif

// COMMON/Protocol802_11.h
//----------------------------------------------------------------------------
//      802.11 Management frame Status Code field
//----------------------------------------------------------------------------
typedef struct _OCTET_STRING{
	u8		*Octet;
	u16		Length;
} OCTET_STRING, *POCTET_STRING;


//======================================================================================
// AES_CCMP specific
//======================================================================================
enum
{
	AESCCMP_BLK_SIZE		=   16,     // # octets in an AES block
	AESCCMP_MAX_PACKET 		=   4*512,  // largest packet size
	AESCCMP_N_RESERVED		=   0,      // reserved nonce octet value
	AESCCMP_A_DATA			=   0x40,   // the Adata bit in the flags
	AESCCMP_M_SHIFT			=   3,      // how much to shift the 3-bit M field
	AESCCMP_L_SHIFT			=   0,      // how much to shift the 3-bit L field
	AESCCMP_L_SIZE			=   2,       // size of the l(m) length field (in octets)
	AESCCMP_OFFSET_SC		=	22,
	AESCCMP_OFFSET_DURATION	=	4,
	AESCCMP_OFFSET_A2		=	10,
	AESCCMP_OFFSET_A4		=	24,
	AESCCMP_QC_TID_MASK		=	0x0f,
	AESCCMP_BLK_SIZE_TOTAL	=   16*16,     // Added by Annie for CKIP AES MIC BSOD, 2006-08-17.
											// 16*8 < 4*60  Resove to 16*16
};

//
// Key Length
//
#define PMK_LEN					32
#define PTK_LEN_TKIP			64
#define GTK_LEN					32
#define KEY_NONCE_LEN			32


// COMMON/Dot11d.h
typedef struct _CHNL_TXPOWER_TRIPLE
{
	u8 FirstChnl;
	u8 NumChnls;
	s8 MaxTxPowerInDbm;
} CHNL_TXPOWER_TRIPLE, *PCHNL_TXPOWER_TRIPLE;


// ===== Below this line is sync from SD7 driver COMMON/bt_hci.h =====
#define BT_THREAD		0
#if(BT_THREAD == 1)
#define SENDTXMEHTOD	2
#else
#define SENDTXMEHTOD	1  // 0=workitem, 1= SendDirectily, 2=thread
#endif

//=============================================
// The following is for BT 3.0 + HS HCI COMMAND ERRORS CODES
//=============================================

#define Max80211PALPDUSize			1492
#define Max80211AMPASSOCLen			672
#define MinGUserPrio					4
#define MaxGUserPrio					7
#define BEUserPrio0						0
#define BEUserPrio1						3
#define Max80211BeaconPeriod		2000
#define ShortRangeModePowerMax		4

#define BT_Default_Chnl					10
#define ACLDataHeaderLen				4

#define BTTotalDataBlockNum			0x100
#define BTLocalBufNum					0x200
#define BTMaxDataBlockLen				0x800
#define BTTOTALBANDWIDTH				0x7530
#define BTMAXBANDGUBANDWIDTH		0x4e20
#define TmpLocalBufSize					0x100
#define BTSynDataPacketLength			0xff
//=============================================

#define BTMaxAuthCount					5
#define BTMaxAsocCount					5

#define MAX_LOGICAL_LINK_NUM			2	//temporarily define
#define MAX_BT_ASOC_ENTRY_NUM		2	//temporarily define

#define INVALID_PL_HANDLE				0xff
#define INVALID_ENTRY_NUM				0xff
//=============================================

#define CAM_BT_START_INDEX		(HALF_CAM_ENTRY - 4)   // MAX_BT_ASOC_ENTRY_NUM : 4 !!!
#define BT_HWCAM_STAR    		CAM_BT_START_INDEX  // We used  HALF_CAM_ENTRY ~ HALF_CAM_ENTRY -MAX_BT_ASOC_ENTRY_NUM

typedef enum _HCI_STATUS
{
	HCI_STATUS_SUCCESS										=0x00, //Success
	HCI_STATUS_UNKNOW_HCI_CMD								=0x01, //Unknown HCI Command
	HCI_STATUS_UNKNOW_CONNECT_ID							=0X02, //Unknown Connection Identifier
	HCI_STATUS_HW_FAIL										=0X03, //Hardware Failure
	HCI_STATUS_PAGE_TIMEOUT									=0X04, //Page Timeout
	HCI_STATUS_AUTH_FAIL										=0X05, //Authentication Failure
	HCI_STATUS_PIN_OR_KEY_MISSING							=0X06, //PIN or Key Missing
	HCI_STATUS_MEM_CAP_EXCEED								=0X07, //Memory Capacity Exceeded
	HCI_STATUS_CONNECT_TIMEOUT								=0X08, //Connection Timeout
	HCI_STATUS_CONNECT_LIMIT									=0X09, //Connection Limit Exceeded
	HCI_STATUS_SYN_CONNECT_LIMIT								=0X0a, //Synchronous Connection Limit To A Device Exceeded
	HCI_STATUS_ACL_CONNECT_EXISTS							=0X0b, //ACL Connection Already Exists
	HCI_STATUS_CMD_DISALLOW									=0X0c, //Command Disallowed
	HCI_STATUS_CONNECT_RJT_LIMIT_RESOURCE					=0X0d, //Connection Rejected due to Limited Resources
	HCI_STATUS_CONNECT_RJT_SEC_REASON						=0X0e, //Connection Rejected Due To Security Reasons
	HCI_STATUS_CONNECT_RJT_UNACCEPT_BD_ADDR				=0X0f, //Connection Rejected due to Unacceptable BD_ADDR
	HCI_STATUS_CONNECT_ACCEPT_TIMEOUT						=0X10, //Connection Accept Timeout Exceeded
	HCI_STATUS_UNSUPPORT_FEATURE_PARA_VALUE				=0X11, //Unsupported Feature or Parameter Value
	HCI_STATUS_INVALID_HCI_CMD_PARA_VALUE					=0X12, //Invalid HCI Command Parameters
	HCI_STATUS_REMOTE_USER_TERMINATE_CONNECT				=0X13, //Remote User Terminated Connection
	HCI_STATUS_REMOTE_DEV_TERMINATE_LOW_RESOURCE			=0X14, //Remote Device Terminated Connection due to Low Resources
	HCI_STATUS_REMOTE_DEV_TERMINATE_CONNECT_POWER_OFF	=0X15, //Remote Device Terminated Connection due to Power Off
	HCI_STATUS_CONNECT_TERMINATE_LOCAL_HOST				=0X16, //Connection Terminated By Local Host
	HCI_STATUS_REPEATE_ATTEMPT								=0X17, //Repeated Attempts
	HCI_STATUS_PAIR_NOT_ALLOW								=0X18, //Pairing Not Allowed
	HCI_STATUS_UNKNOW_LMP_PDU								=0X19, //Unknown LMP PDU
	HCI_STATUS_UNSUPPORT_REMOTE_LMP_FEATURE				=0X1a, //Unsupported Remote Feature / Unsupported LMP Feature
	HCI_STATUS_SOC_OFFSET_REJECT								=0X1b, //SCO Offset Rejected
	HCI_STATUS_SOC_INTERVAL_REJECT							=0X1c, //SCO Interval Rejected
	HCI_STATUS_SOC_AIR_MODE_REJECT							=0X1d,//SCO Air Mode Rejected
	HCI_STATUS_INVALID_LMP_PARA								=0X1e, //Invalid LMP Parameters
	HCI_STATUS_UNSPECIFIC_ERROR								=0X1f, //Unspecified Error
	HCI_STATUS_UNSUPPORT_LMP_PARA_VALUE					=0X20, //Unsupported LMP Parameter Value
	HCI_STATUS_ROLE_CHANGE_NOT_ALLOW						=0X21, //Role Change Not Allowed
	HCI_STATUS_LMP_RESPONSE_TIMEOUT							=0X22, //LMP Response Timeout
	HCI_STATUS_LMP_ERROR_TRANSACTION_COLLISION				=0X23, //LMP Error Transaction Collision
	HCI_STATUS_LMP_PDU_NOT_ALLOW							=0X24, //LMP PDU Not Allowed
	HCI_STATUS_ENCRYPTION_MODE_NOT_ALLOW					=0X25, //Encryption Mode Not Acceptable
	HCI_STATUS_LINK_KEY_CAN_NOT_CHANGE						=0X26, //Link Key Can Not be Changed
	HCI_STATUS_REQUEST_QOS_NOT_SUPPORT						=0X27, //Requested QoS Not Supported
	HCI_STATUS_INSTANT_PASSED								=0X28, //Instant Passed
	HCI_STATUS_PAIRING_UNIT_KEY_NOT_SUPPORT					=0X29, //Pairing With Unit Key Not Supported
	HCI_STATUS_DIFFERENT_TRANSACTION_COLLISION				=0X2a, //Different Transaction Collision
	HCI_STATUS_RESERVE_1										=0X2b, //Reserved
	HCI_STATUS_QOS_UNACCEPT_PARA							=0X2c, //QoS Unacceptable Parameter
	HCI_STATUS_QOS_REJECT										=0X2d, //QoS Rejected
	HCI_STATUS_CHNL_CLASSIFICATION_NOT_SUPPORT				=0X2e, //Channel Classification Not Supported
	HCI_STATUS_INSUFFICIENT_SECURITY							=0X2f, //Insufficient Security
	HCI_STATUS_PARA_OUT_OF_RANGE							=0x30, //Parameter Out Of Mandatory Range
	HCI_STATUS_RESERVE_2										=0X31, //Reserved
	HCI_STATUS_ROLE_SWITCH_PENDING							=0X32, //Role Switch Pending
	HCI_STATUS_RESERVE_3										=0X33, //Reserved
	HCI_STATUS_RESERVE_SOLT_VIOLATION						=0X34, //Reserved Slot Violation
	HCI_STATUS_ROLE_SWITCH_FAIL								=0X35, //Role Switch Failed
	HCI_STATUS_EXTEND_INQUIRY_RSP_TOO_LARGE				=0X36, //Extended Inquiry Response Too Large
	HCI_STATUS_SEC_SIMPLE_PAIRING_NOT_SUPPORT				=0X37, //Secure Simple Pairing Not Supported By Host.
	HCI_STATUS_HOST_BUSY_PAIRING								=0X38, //Host Busy - Pairing
	HCI_STATUS_CONNECT_REJ_NOT_SUIT_CHNL_FOUND			=0X39, //Connection Rejected due to No Suitable Channel Found
	HCI_STATUS_CONTROLLER_BUSY								=0X3a  //CONTROLLER BUSY
} HCI_STATUS,*PHCI_STATUS;

//=============================================
// The following is for BT 3.0 + HS HCI COMMAND
//=============================================

//bit 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
//	 |	OCF			             |	   OGF       |
//

//OGF 0x01
#define OGF_LINK_CONTROL_COMMANDS						0x01
typedef enum _LINK_CONTROL_COMMANDS
{
	HCI_INQUIRY											=0x0001,
	HCI_INQUIRY_CANCEL									=0x0002,
	HCI_PERIODIC_INQUIRY_MODE							=0x0003,
	HCI_EXIT_PERIODIC_INQUIRY_MODE						=0x0004,
	HCI_CREATE_CONNECTION								=0x0005,
	HCI_DISCONNECT										=0x0006,
	HCI_CREATE_CONNECTION_CANCEL						=0x0008,
	HCI_ACCEPT_CONNECTIONREQUEST						=0x0009,
	HCI_REJECT_CONNECTION_REQUEST						=0x000a,
	HCI_LINK_KEY_REQUEST_REPLY							=0x000b,
	HCI_LINK_KEY_REQUEST_NEGATIVE_REPLY					=0x000c,
	HCI_PIN_CODE_REQUEST_REPLY							=0x000d,
	HCI_PIN_CODE_REQUEST_NEGATIVE_REPLY				=0x000e,
	HCI_CHANGE_CONNECTION_PACKET_TYPE					=0x000f,
	HCI_AUTHENTICATION_REQUESTED						=0x0011,
	HCI_SET_CONNECTION_ENCRYPTION						=0x0013,
	HCI_CHANGE_CONNECTION_LINK_KEY						=0x0015,
	HCI_MASTER_LINK_KEY									=0x0017,
	HCI_REMOTE_NAME_REQUEST								=0x0019,
	HCI_REMOTE_NAME_REQUEST_CANCEL						=0x001a,
	HCI_READ_REMOTE_SUPPORTED_FEATURES				=0x001b,
	HCI_READ_REMOTE_EXTENDED_FEATURES					=0x001c,
	HCI_READ_REMOTE_VERSION_INFORMATION				=0x001d,
	HCI_READ_CLOCK_OFFSET								=0x001f,
	HCI_READ_LMP_HANDLE									=0x0020,
	HCI_SETUP_SYNCHRONOUS_CONNECTION					=0x0028,
	HCI_ACCEPT_SYNCHRONOUS_CONNECTION_REQUEST		=0x0029,
	HCI_REJECT_SYNCHRONOUS_CONNECTION_REQUEST		=0x002a,
	HCI_IO_CAPABILITY_REQUEST_REPLY						=0x002b,
	HCI_USER_CONFIRMATION_REQUEST_REPLY				=0x002c,
	HCI_USER_CONFIRMATION_REQUEST_NEGATIVE_REPLY		=0x002d,
	HCI_USER_PASSKEY_REQUEST_REPLY						=0x002e,
	HCI_USER_PASSKEY_REQUESTNEGATIVE_REPLY				=0x002f,
	HCI_REMOTE_OOB_DATA_REQUEST_REPLY					=0x0030,
	HCI_REMOTE_OOB_DATA_REQUEST_NEGATIVE_REPLY		=0x0033,
	HCI_IO_CAPABILITY_REQUEST_NEGATIVE_REPLY			=0x0034,
	HCI_CREATE_PHYSICAL_LINK								=0x0035,
	HCI_ACCEPT_PHYSICAL_LINK								=0x0036,
	HCI_DISCONNECT_PHYSICAL_LINK							=0x0037,
	HCI_CREATE_LOGICAL_LINK								=0x0038,
	HCI_ACCEPT_LOGICAL_LINK								=0x0039,
	HCI_DISCONNECT_LOGICAL_LINK							=0x003a,
	HCI_LOGICAL_LINK_CANCEL								=0x003b,
	HCI_FLOW_SPEC_MODIFY									=0x003c
} LINK_CONTROL_COMMANDS,*PLINK_CONTROL_COMMANDS;

//OGF 0x02
#define OGF_HOLD_MODE_COMMAND							0x02
typedef enum _HOLD_MODE_COMMAND
{
	HCI_HOLD_MODE											=0x0001,
	HCI_SNIFF_MODE										=0x0002,
	HCI_EXIT_SNIFF_MODE									=0x0003,
	HCI_PARK_STATE											=0x0005,
	HCI_EXIT_PARK_STATE									=0x0006,
	HCI_QOS_SETUP											=0x0007,
	HCI_ROLE_DISCOVERY									=0x0009,
	HCI_SWITCH_ROLE										=0x000b,
	HCI_READ_LINK_POLICY_SETTINGS						=0x000c,
	HCI_WRITE_LINK_POLICY_SETTINGS						=0x000d,
	HCI_READ_DEFAULT_LINK_POLICY_SETTINGS				=0x000e,
	HCI_WRITE_DEFAULT_LINK_POLICY_SETTINGS				=0x000f,
	HCI_FLOW_SPECIFICATION								=0x0010,
	HCI_SNIFF_SUBRATING									=0x0011
} HOLD_MODE_COMMAND,*PHOLD_MODE_COMMAND;

//OGF 0x03
#define OGF_SET_EVENT_MASK_COMMAND						0x03
typedef enum _SET_EVENT_MASK_COMMAND
{
	HCI_SET_EVENT_MASK									=0x0001,
	HCI_RESET												=0x0003,
	HCI_SET_EVENT_FILTER									=0x0005,
	HCI_FLUSH												=0x0008,
	HCI_READ_PIN_TYPE										=0x0009,
	HCI_WRITE_PIN_TYPE									=0x000a,
	HCI_CREATE_NEW_UNIT_KEY								=0x000b,
	HCI_READ_STORED_LINK_KEY								=0x000d,
	HCI_WRITE_STORED_LINK_KEY							=0x0011,
	HCI_DELETE_STORED_LINK_KEY							=0x0012,
	HCI_WRITE_LOCAL_NAME									=0x0013,
	HCI_READ_LOCAL_NAME									=0x0014,
	HCI_READ_CONNECTION_ACCEPT_TIMEOUT					=0x0015,
	HCI_WRITE_CONNECTION_ACCEPT_TIMEOUT				=0x0016,
	HCI_READ_PAGE_TIMEOUT								=0x0017,
	HCI_WRITE_PAGE_TIMEOUT								=0x0018,
	HCI_READ_SCAN_ENABLE									=0x0019,
	HCI_WRITE_SCAN_ENABLE								=0x001a,
	HCI_READ_PAGE_SCAN_ACTIVITY							=0x001b,
	HCI_WRITE_PAGE_SCAN_ACTIVITY							=0x001c,
	HCI_READ_INQUIRY_SCAN_ACTIVITY						=0x001d,
	HCI_WRITE_INQUIRY_SCAN_ACTIVITY						=0x001e,
	HCI_READ_AUTHENTICATION_ENABLE						=0x001f,
	HCI_WRITE_AUTHENTICATION_ENABLE						=0x0020,
	HCI_READ_CLASS_OF_DEVICE								=0x0023,
	HCI_WRITE_CLASS_OF_DEVICE							=0x0024,
	HCI_READ_VOICE_SETTING								=0x0025,
	HCI_WRITE_VOICE_SETTING								=0x0026,
	HCI_READ_AUTOMATIC_FLUSH_TIMEOUT					=0x0027,
	HCI_WRITE_AUTOMATIC_FLUSH_TIMEOUT					=0x0028,
	HCI_READ_NUM_BROADCAST_RETRANSMISSIONS			=0x0029,
	HCI_WRITE_NUM_BROADCAST_RETRANSMISSIONS			=0x002a,
	HCI_READ_HOLD_MODE_ACTIVITY							=0x002b,
	HCI_WRITE_HOLD_MODE_ACTIVITY						=0x002c,
	HCI_READ_SYNCHRONOUS_FLOW_CONTROL_ENABLE		=0x002e,
	HCI_WRITE_SYNCHRONOUS_FLOW_CONTROL_ENABLE		=0x002f,
	HCI_SET_CONTROLLER_TO_HOST_FLOW_CONTROL			=0x0031,
	HCI_HOST_BUFFER_SIZE									=0x0033,
	HCI_HOST_NUMBER_OF_COMPLETED_PACKETS				=0x0035,
	HCI_READ_LINK_SUPERVISION_TIMEOUT					=0x0036,
	HCI_WRITE_LINK_SUPERVISION_TIMEOUT					=0x0037,
	HCI_READ_NUMBER_OF_SUPPORTED_IAC					=0x0038,
	HCI_READ_CURRENT_IAC_LAP								=0x0039,
	HCI_WRITE_CURRENT_IAC_LAP							=0x003a,
	HCI_READ_PAGE_SCAN_MODE								=0x003d,
	HCI_WRITE_PAGE_SCAN_MODE							=0x003e,
	HCI_SET_AFH_HOST_CHANNEL_CLASSIFICATION			=0x003f,
	HCI_READ_INQUIRY_SCAN_TYPE							=0x0042,
	HCI_WRITE_INQUIRY_SCAN_TYPE							=0x0043,
	HCI_READ_INQUIRY_MODE								=0x0044,
	HCI_WRITE_INQUIRY_MODE								=0x0045,
	HCI_READ_PAGE_SCAN_TYPE								=0x0046,
	HCI_WRITE_PAGE_SCAN_TYPE								=0x0047,
	HCI_READ_AFH_CHANNEL_ASSESSMENT_MODE				=0x0048,
	HCI_WRITE_AFH_CHANNEL_ASSESSMENT_MODE			=0x0049,
	HCI_READ_EXTENDED_INQUIRY_RESPONSE					=0x0051,
	HCI_WRITE_EXTENDED_INQUIRY_RESPONSE				=0x0052,
	HCI_REFRESH_ENCRYPTION_KEY							=0x0053,
	HCI_READ_SIMPLE_PAIRING_MODE						=0x0055,
	HCI_WRITE_SIMPLE_PAIRING_MODE						=0x0056,
	HCI_READ_LOCAL_OOB_DATA								=0x0057,
	HCI_READ_INQUIRY_RESPONSE_TRANSMIT_POWER_LEVEL	=0x0058,
	HCI_WRITE_INQUIRY_TRANSMIT_POWER_LEVEL				=0x0059,
	HCI_READ_DEFAULT_ERRONEOUS_DATA_REPORTING			=0x005a,
	HCI_WRITE_DEFAULT_ERRONEOUS_DATA_REPORTING		=0x005b,
	HCI_ENHANCED_FLUSH									=0x005f,
	HCI_SEND_KEYPRESS_NOTIFICATION						=0x0060,
	HCI_READ_LOGICAL_LINK_ACCEPT_TIMEOUT				=0x0061,
	HCI_WRITE_LOGICAL_LINK_ACCEPT_TIMEOUT				=0x0062,
	HCI_SET_EVENT_MASK_PAGE_2							=0x0063,
	HCI_READ_LOCATION_DATA								=0x0064,
	HCI_WRITE_LOCATION_DATA								=0x0065,
	HCI_READ_FLOW_CONTROL_MODE							=0x0066,
	HCI_WRITE_FLOW_CONTROL_MODE						=0x0067,
	HCI_READ_ENHANCE_TRANSMIT_POWER_LEVEL				=0x0068,
	HCI_READ_BEST_EFFORT_FLUSH_TIMEOUT					=0x0069,
	HCI_WRITE_BEST_EFFORT_FLUSH_TIMEOUT				=0x006a,
	HCI_SHORT_RANGE_MODE								=0x006b
}SET_EVENT_MASK_COMMAND,*PSET_EVENT_MASK_COMMAND;

//OGF 0x04
#define OGF_INFORMATIONAL_PARAMETERS						0x04
typedef enum _INFORMATIONAL_PARAMETERS
{
	HCI_READ_LOCAL_VERSION_INFORMATION				=0x0001,
	HCI_READ_LOCAL_SUPPORTED_COMMANDS				=0x0002,
	HCI_READ_LOCAL_SUPPORTED_FEATURES				=0x0003,
	HCI_READ_LOCAL_EXTENDED_FEATURES				=0x0004,
	HCI_READ_BUFFER_SIZE								=0x0005,
	HCI_READ_BD_ADDR									=0x0009,
	HCI_READ_DATA_BLOCK_SIZE							=0x000a
} INFORMATIONAL_PARAMETERS,*PINFORMATIONAL_PARAMETERS;

//OGF 0x05
#define OGF_STATUS_PARAMETERS								0x05
typedef enum _STATUS_PARAMETERS
{
	HCI_READ_FAILED_CONTACT_COUNTER						=0x0001,
	HCI_RESET_FAILED_CONTACT_COUNTER					=0x0002,
	HCI_READ_LINK_QUALITY									=0x0003,
	HCI_READ_RSSI											=0x0005,
	HCI_READ_AFH_CHANNEL_MAP							=0x0006,
	HCI_READ_CLOCK										=0x0007,
	HCI_READ_ENCRYPTION_KEY_SIZE							=0x0008,
	HCI_READ_LOCAL_AMP_INFO								=0x0009,
	HCI_READ_LOCAL_AMP_ASSOC							=0x000a,
	HCI_WRITE_REMOTE_AMP_ASSOC							=0x000b
} STATUS_PARAMETERS,*PSTATUS_PARAMETERS;

//OGF 0x06
#define OGF_TESTING_COMMANDS								0x06
typedef enum _TESTING_COMMANDS
{
	HCI_READ_LOOPBACK_MODE							=0x0001,
	HCI_WRITE_LOOPBACK_MODE							=0x0002,
	HCI_ENABLE_DEVICE_UNDER_TEST_MODE				=0x0003,
	HCI_WRITE_SIMPLE_PAIRING_DEBUG_MODE			=0x0004,
	HCI_ENABLE_AMP_RECEIVER_REPORTS					=0x0007,
	HCI_AMP_TEST_END									=0x0008,
	HCI_AMP_TEST_COMMAND							=0x0009
} TESTING_COMMANDS,*PTESTING_COMMANDS;

//OGF 0x3f
#define OGF_EXTENSION							0X3f
typedef enum _HCI_EXTENSION_COMMANDS
{
	HCI_SET_ACL_LINK_DATA_FLOW_MODE				=0x0010,
	HCI_SET_ACL_LINK_STATUS							=0x0020,
	HCI_SET_SCO_LINK_STATUS							=0x0030,
	HCI_SET_RSSI_VALUE								=0x0040,
	HCI_SET_CURRENT_BLUETOOTH_STATUS				=0x0041,

	//The following is for RTK8723
	HCI_EXTENSION_VERSION_NOTIFY					=0x0100,
	HCI_LINK_STATUS_NOTIFY							=0x0101,
	HCI_BT_OPERATION_NOTIFY							=0x0102,
	HCI_ENABLE_WIFI_SCAN_NOTIFY 						=0x0103,


	//The following is for IVT
	HCI_WIFI_CURRENT_CHANNEL						=0x0300,
	HCI_WIFI_CURRENT_BANDWIDTH						=0x0301,
	HCI_WIFI_CONNECTION_STATUS						=0x0302,
} HCI_EXTENSION_COMMANDS,*PHCI_EXTENSION_COMMANDS;

typedef enum _BT_SPEC
{
	BT_SPEC_1_0_b										=0x00,
	BT_SPEC_1_1										=0x01,
	BT_SPEC_1_2										=0x02,
	BT_SPEC_2_0_EDR									=0x03,
	BT_SPEC_2_1_EDR									=0x04,
	BT_SPEC_3_0_HS									=0x05,
	BT_SPEC_4_0										=0x06
} BT_SPEC,*PBT_SPEC;

//=============================================
// The following is for BT 3.0 + HS EVENTS
//=============================================
typedef enum _HCI_EVENT
{
	HCI_EVENT_INQUIRY_COMPLETE								=0x01,
	HCI_EVENT_INQUIRY_RESULT									=0x02,
	HCI_EVENT_CONNECTION_COMPLETE							=0x03,
	HCI_EVENT_CONNECTION_REQUEST							=0x04,
	HCI_EVENT_DISCONNECTION_COMPLETE						=0x05,
	HCI_EVENT_AUTHENTICATION_COMPLETE						=0x06,
	HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE				=0x07,
	HCI_EVENT_ENCRYPTION_CHANGE								=0x08,
	HCI_EVENT_CHANGE_LINK_KEY_COMPLETE						=0x09,
	HCI_EVENT_MASTER_LINK_KEY_COMPLETE						=0x0a,
	HCI_EVENT_READ_REMOTE_SUPPORT_FEATURES_COMPLETE		=0x0b,
	HCI_EVENT_READ_REMOTE_VER_INFO_COMPLETE				=0x0c,
	HCI_EVENT_QOS_SETUP_COMPLETE							=0x0d,
	HCI_EVENT_COMMAND_COMPLETE								=0x0e,
	HCI_EVENT_COMMAND_STATUS								=0x0f,
	HCI_EVENT_HARDWARE_ERROR								=0x10,
	HCI_EVENT_FLUSH_OCCRUED									=0x11,
	HCI_EVENT_ROLE_CHANGE									=0x12,
	HCI_EVENT_NUMBER_OF_COMPLETE_PACKETS					=0x13,
	HCI_EVENT_MODE_CHANGE									=0x14,
	HCI_EVENT_RETURN_LINK_KEYS								=0x15,
	HCI_EVENT_PIN_CODE_REQUEST								=0x16,
	HCI_EVENT_LINK_KEY_REQUEST								=0x17,
	HCI_EVENT_LINK_KEY_NOTIFICATION							=0x18,
	HCI_EVENT_LOOPBACK_COMMAND								=0x19,
	HCI_EVENT_DATA_BUFFER_OVERFLOW							=0x1a,
	HCI_EVENT_MAX_SLOTS_CHANGE								=0x1b,
	HCI_EVENT_READ_CLOCK_OFFSET_COMPLETE					=0x1c,
	HCI_EVENT_CONNECT_PACKET_TYPE_CHANGE					=0x1d,
	HCI_EVENT_QOS_VIOLATION									=0x1e,
	HCI_EVENT_PAGE_SCAN_REPETITION_MODE_CHANGE			=0x20,
	HCI_EVENT_FLOW_SEPC_COMPLETE							=0x21,
	HCI_EVENT_INQUIRY_RESULT_WITH_RSSI						=0x22,
	HCI_EVENT_READ_REMOTE_EXT_FEATURES_COMPLETE			=0x23,
	HCI_EVENT_SYNC_CONNECT_COMPLETE						=0x2c,
	HCI_EVENT_SYNC_CONNECT_CHANGE							=0x2d,
	HCI_EVENT_SNIFFER_SUBRATING								=0x2e,
	HCI_EVENT_EXTENTED_INQUIRY_RESULT						=0x2f,
	HCI_EVENT_ENCRYPTION_KEY_REFLASH_COMPLETE				=0x30,
	HCI_EVENT_IO_CAPIBILITY_COMPLETE							=0x31,
	HCI_EVENT_IO_CAPIBILITY_RESPONSE							=0x32,
	HCI_EVENT_USER_CONFIRMTION_REQUEST						=0x33,
	HCI_EVENT_USER_PASSKEY_REQUEST							=0x34,
	HCI_EVENT_REMOTE_OOB_DATA_REQUEST						=0x35,
	HCI_EVENT_SIMPLE_PAIRING_COMPLETE						=0x36,
	HCI_EVENT_LINK_SUPERVISION_TIMEOUT_CHANGE				=0x38,
	HCI_EVENT_ENHANCED_FLUSH_COMPLETE						=0x39,
	HCI_EVENT_USER_PASSKEY_NOTIFICATION						=0x3b,
	HCI_EVENT_KEYPRESS_NOTIFICATION							=0x3c,
	HCI_EVENT_REMOTE_HOST_SUPPORT_FEATURES_NOTIFICATION	=0x3d,
	HCI_EVENT_PHY_LINK_COMPLETE								=0x40,
	HCI_EVENT_CHANNEL_SELECT									=0x41,
	HCI_EVENT_DISCONNECT_PHY_LINK_COMPLETE					=0x42,
	HCI_EVENT_PHY_LINK_LOSS_EARLY_WARNING					=0x43,
	HCI_EVENT_PHY_LINK_RECOVER								=0x44,
	HCI_EVENT_LOGICAL_LINK_COMPLETE							=0x45,
	HCI_EVENT_DISCONNECT_LOGICAL_LINK_COMPLETE				=0x46,
	HCI_EVENT_FLOW_SPEC_MODIFY_COMPLETE					=0x47,
	HCI_EVENT_NUM_OF_COMPLETE_DATA_BLOCKS					=0x48,
	HCI_EVENT_AMP_START_TEST								=0x49,
	HCI_EVENT_AMP_TEST_END									=0x4a,
	HCI_EVENT_AMP_RECEIVER_REPORT							=0x4b,
	HCI_EVENT_SHORT_RANGE_MODE_CHANGE_COMPLETE			=0x4c,
	HCI_EVENT_AMP_STATUS_CHANGE							=0x4d,
	HCI_EVENT_EXTENSION_RTK									=0xfe,
	HCI_EVENT_EXTENSION_MOTO								=0xff,
}HCI_EVENT, *PHCI_EVENT;

typedef enum _HCI_EXTENSION_EVENT_MOTO
{
	HCI_EVENT_GET_BT_RSSI										=0x01,
} HCI_EXTENSION_EVENT_MOTO, *PHCI_EXTENSION_EVENT_MOTO;

typedef enum _HCI_EXTENSION_EVENT_RTK
{
	HCI_EVENT_EXT_WIFI_SCAN_NOTIFY								=0x01,
} HCI_EXTENSION_EVENT_RTK, *PHCI_EXTENSION_EVENT_RTK;

typedef enum _HCI_EVENT_MASK_PAGE_2
{
	EMP2_HCI_EVENT_PHY_LINK_COMPLETE						=0x0000000000000001,
	EMP2_HCI_EVENT_CHANNEL_SELECT							=0x0000000000000002,
	EMP2_HCI_EVENT_DISCONNECT_PHY_LINK_COMPLETE			=0x0000000000000004,
	EMP2_HCI_EVENT_PHY_LINK_LOSS_EARLY_WARNING			=0x0000000000000008,
	EMP2_HCI_EVENT_PHY_LINK_RECOVER							=0x0000000000000010,
	EMP2_HCI_EVENT_LOGICAL_LINK_COMPLETE					=0x0000000000000020,
	EMP2_HCI_EVENT_DISCONNECT_LOGICAL_LINK_COMPLETE		=0x0000000000000040,
	EMP2_HCI_EVENT_FLOW_SPEC_MODIFY_COMPLETE				=0x0000000000000080,
	EMP2_HCI_EVENT_NUM_OF_COMPLETE_DATA_BLOCKS			=0x0000000000000100,
	EMP2_HCI_EVENT_AMP_START_TEST							=0x0000000000000200,
	EMP2_HCI_EVENT_AMP_TEST_END								=0x0000000000000400,
	EMP2_HCI_EVENT_AMP_RECEIVER_REPORT						=0x0000000000000800,
	EMP2_HCI_EVENT_SHORT_RANGE_MODE_CHANGE_COMPLETE	=0x0000000000001000,
	EMP2_HCI_EVENT_AMP_STATUS_CHANGE						=0x0000000000002000,
} HCI_EVENT_MASK_PAGE_2, *PHCI_EVENT_MASK_PAGE_2;

typedef enum _HCI_STATE_MACHINE
{
	HCI_STATE_STARTING				=0x01,
	HCI_STATE_CONNECTING			=0x02,
	HCI_STATE_AUTHENTICATING		=0x04,
	HCI_STATE_CONNECTED			=0x08,
	HCI_STATE_DISCONNECTING		=0x10,
	HCI_STATE_DISCONNECTED 		=0x20
} HCI_STATE_MACHINE, *PHCI_STATE_MACHINE;

typedef enum _AMP_ASSOC_STRUCTURE_TYPE
{
	AMP_MAC_ADDR					=0x01,
	AMP_PREFERRED_CHANNEL_LIST	=0x02,
	AMP_CONNECTED_CHANNEL		=0x03,
	AMP_80211_PAL_CAP_LIST		=0x04,
	AMP_80211_PAL_VISION			=0x05,
	AMP_RESERVED_FOR_TESTING		=0x33
} AMP_ASSOC_STRUCTURE_TYPE, *PAMP_ASSOC_STRUCTURE_TYPE;

typedef enum _AMP_BTAP_TYPE
{
	AMP_BTAP_NONE,
	AMP_BTAP_CREATOR,
	AMP_BTAP_JOINER
} AMP_BTAP_TYPE, *PAMP_BTAP_TYPE;

typedef enum _HCI_STATE_WITH_CMD
{
	STATE_CMD_CREATE_PHY_LINK,
	STATE_CMD_ACCEPT_PHY_LINK,
	STATE_CMD_DISCONNECT_PHY_LINK,
	STATE_CMD_CONNECT_ACCEPT_TIMEOUT,
	STATE_CMD_MAC_START_COMPLETE,
	STATE_CMD_MAC_START_FAILED,
	STATE_CMD_MAC_CONNECT_COMPLETE,
	STATE_CMD_MAC_CONNECT_FAILED,
	STATE_CMD_MAC_DISCONNECT_INDICATE,
	STATE_CMD_MAC_CONNECT_CANCEL_INDICATE,
	STATE_CMD_4WAY_FAILED,
	STATE_CMD_4WAY_SUCCESSED,
	STATE_CMD_ENTER_STATE,
	STATE_CMD_NO_SUCH_CMD,
} HCI_STATE_WITH_CMD, *PHCI_STATE_WITH_CMD;

typedef enum _HCI_SERVICE_TYPE
{
	SERVICE_NO_TRAFFIC,
	SERVICE_BEST_EFFORT,
	SERVICE_GUARANTEE
} HCI_SERVICE_TYPE, *PHCI_SERVICE_TYPE;

typedef enum _HCI_TRAFFIC_MODE
{
	TRAFFIC_MODE_BEST_EFFORT									=0x00,
	TRAFFIC_MODE_GUARANTEED_LATENCY						=0x01,
	TRAFFIC_MODE_GUARANTEED_BANDWIDTH						=0x02,
	TRAFFIC_MODE_GUARANTEED_LATENCY_AND_BANDWIDTH		=0x03
} HCI_TRAFFIC_MODE, *PHCI_TRAFFIC_MODE;

#define HCIOPCODE(_OCF, _OGF)		(_OGF<<10|_OCF)
#define HCIOPCODELOW(_OCF, _OGF)	(u8)(HCIOPCODE(_OCF, _OGF)&0x00ff)
#define HCIOPCODEHIGHT(_OCF, _OGF)	(u8)(HCIOPCODE(_OCF, _OGF)>>8)

#define TWOBYTE_HIGHTBYTE(_DATA)	(u8)(_DATA>>8)
#define TWOBYTE_LOWBYTE(_DATA)		(u8)(_DATA)

typedef enum _AMP_STATUS
{
	AMP_STATUS_AVA_PHY_PWR_DWN		= 0x0,
	AMP_STATUS_BT_USE_ONLY				= 0x1,
	AMP_STATUS_NO_CAPACITY_FOR_BT		= 0x2,
	AMP_STATUS_LOW_CAPACITY_FOR_BT		= 0x3,
	AMP_STATUS_MEDIUM_CAPACITY_FOR_BT	= 0x4,
	AMP_STATUS_HIGH_CAPACITY_FOR_BT		= 0x5,
	AMP_STATUS_FULL_CAPACITY_FOR_BT		= 0x6
} AMP_STATUS, *PAMP_STATUS;

typedef enum
{
	Type_BT_4way1st	= 0,
	Type_BT_4way2nd	= 1,
	Type_BT_4way3rd	= 2,
	Type_BT_4way4th	= 3,
	Type_BT_unknow	= 4
} BT_WPAMsgType;

typedef enum _BT_CONNECT_TYPE
{
	BT_CONNECT_AUTH_REQ								=0x00,
	BT_CONNECT_AUTH_RSP								=0x01,
	BT_CONNECT_ASOC_REQ								=0x02,
	BT_CONNECT_ASOC_RSP								=0x03,
	BT_DISCONNECT										=0x04
} BT_CONNECT_TYPE, *PBT_CONNECT_TYPE;

typedef enum _BT_LL_SERVICE_TYPE
{
	BT_LL_BE = 0x01,
	BT_LL_GU = 0x02
} BT_LL_SERVICE_TYPE;

typedef enum _BT_LL_FLOWSPEC
{
	BT_TX_BE_FS,				//TX best effort flowspec
	BT_RX_BE_FS,				//RX best effort flowspec
	BT_TX_GU_FS,				//TX guaranteed latency flowspec
	BT_RX_GU_FS,				//RX guaranteed latency flowspec
	BT_TX_BE_AGG_FS,			//TX aggregated best effort flowspec
	BT_RX_BE_AGG_FS,			//RX aggregated best effort flowspec
	BT_TX_GU_BW_FS,			//TX guaranteed bandwidth flowspec
	BT_RX_GU_BW_FS,			//RX guaranteed bandwidth flowspec
	BT_TX_GU_LARGE_FS,		//TX guaranteed latency flowspec, for testing only
	BT_RX_GU_LARGE_FS,		//RX guaranteed latency flowspec, for testing only
} BT_LL_FLOWSPEC;

typedef enum _BT_TRAFFIC_MODE
{
	BT_MOTOR_EXT_BE		= 0x00, //Best Effort. Default. for HCRP, PAN, SDP, RFCOMM-based profiles like FTP,OPP, SPP, DUN, etc.
	BT_MOTOR_EXT_GUL	= 0x01, //Guaranteed Latency. This type of traffic is used e.g. for HID and AVRCP.
	BT_MOTOR_EXT_GUB	= 0X02, //Guaranteed Bandwidth.
	BT_MOTOR_EXT_GULB	= 0X03  //Guaranteed Latency and Bandwidth. for A2DP and VDP.
} BT_TRAFFIC_MODE;

typedef enum _BT_TRAFFIC_MODE_PROFILE
{
	BT_PROFILE_NONE,
	BT_PROFILE_A2DP,
	BT_PROFILE_PAN,
	BT_PROFILE_HID,
	BT_PROFILE_SCO
} BT_TRAFFIC_MODE_PROFILE;

typedef enum _BT_LINK_ROLE
{
	BT_LINK_MASTER	= 0,
	BT_LINK_SLAVE	= 1
} BT_LINK_ROLE;

typedef enum _BT_STATE_WPA_AUTH
{
	STATE_WPA_AUTH_UNINITIALIZED,
	STATE_WPA_AUTH_WAIT_PACKET_1, // Join
	STATE_WPA_AUTH_WAIT_PACKET_2, // Creat
	STATE_WPA_AUTH_WAIT_PACKET_3,
	STATE_WPA_AUTH_WAIT_PACKET_4,
	STATE_WPA_AUTH_SUCCESSED
} BT_STATE_WPA_AUTH, *PBT_STATE_WPA_AUTH;

#define BT_WPA_AUTH_TIMEOUT_PERIOD		1000
#define BTMaxWPAAuthReTransmitCoun 		5

#define MAX_AMP_ASSOC_FRAG_LEN			248
#define TOTAL_ALLOCIATE_ASSOC_LEN			1000

typedef struct _HCI_FLOW_SPEC
{
	u8				Identifier;
	u8				ServiceType;
	u16				MaximumSDUSize;
	u32				SDUInterArrivalTime;
	u32				AccessLatency;
	u32				FlushTimeout;
} HCI_FLOW_SPEC, *PHCI_FLOW_SPEC;

typedef struct _HCI_LOG_LINK_CMD_DATA
{
	u8				BtPhyLinkhandle;
	u16				BtLogLinkhandle;
	u8				BtTxFlowSpecID;
	HCI_FLOW_SPEC		Tx_Flow_Spec;
	HCI_FLOW_SPEC		Rx_Flow_Spec;
	u32				TxPacketCount;
	u32				BestEffortFlushTimeout;

	u8				bLLCompleteEventIsSet;

	u8				bLLCancelCMDIsSetandComplete;
} HCI_LOG_LINK_CMD_DATA, *PHCI_LOG_LINK_CMD_DATA;

typedef struct _HCI_PHY_LINK_CMD_DATA
{
	//Physical_Link_Handle
	u8				BtPhyLinkhandle;

	u16				LinkSuperversionTimeout;

	//u16				SuperTimeOutCnt;

	//Dedicated_AMP_Key_Length
	u8				BtAMPKeyLen;
	//Dedicated_AMP_Key_Type
	u8				BtAMPKeyType;
	//Dedicated_AMP_Key
	u8				BtAMPKey[PMK_LEN];
} HCI_PHY_LINK_CMD_DATA, *PHCI_PHY_LINK_CMD_DATA;

typedef struct _AMP_ASSOC_STRUCTURE
{
	//TYPE ID
	u8				TypeID;
	//Length
	u16				Length;
	//Value
	u8				Data[1];
} AMP_ASSOC_STRUCTURE, *PAMP_ASSOC_STRUCTURE;

typedef struct _AMP_PREF_CHNL_REGULATORY
{
	u8				reXId;
	u8				regulatoryClass;
	u8				coverageClass;
} AMP_PREF_CHNL_REGULATORY, *PAMP_PREF_CHNL_REGULATORY;

typedef struct _AMP_ASSOC_CMD_DATA
{
	//Physical_Link_Handle
	u8					BtPhyLinkhandle;
	//Length_So_Far
	u16					LenSoFar;

	u16					MaxRemoteASSOCLen;
	//AMP_ASSOC_Remaining_Length
	u16					AMPAssocRemLen;
	//AMP_ASSOC_fragment
	void				*AMPAssocfragment;
} AMP_ASSOC_CMD_DATA, *PAMP_ASSOC_CMD_DATA;

typedef struct _HCI_LINK_INFO
{
	u16					ConnectHandle;
	u8					IncomingTrafficMode;
	u8					OutgoingTrafficMode;
	u8					BTProfile;
	u8					BTCoreSpec;
	s8					BT_RSSI;
	u8					TrafficProfile;
	u8					linkRole;
} HCI_LINK_INFO, *PHCI_LINK_INFO;

typedef struct _HCI_EXT_CONFIG
{
	HCI_LINK_INFO				linkInfo[MAX_BT_ASOC_ENTRY_NUM];
	u8					btOperationCode;
	u16					CurrentConnectHandle;
	u8					CurrentIncomingTrafficMode;
	u8					CurrentOutgoingTrafficMode;
	s8					MIN_BT_RSSI;
	u8					NumberOfHandle;
	u8					NumberOfSCO;
	u8					CurrentBTStatus;
	u16					HCIExtensionVer;

	//Bt coexist related
	u8					btProfileCase;
	u8					btProfileAction;
	u8					bManualControl;
	u8					bBTBusy;
	u8					bBTA2DPBusy;
	u8					bEnableWifiScanNotify;

	u8					bHoldForBtOperation;
}HCI_EXT_CONFIG, *PHCI_EXT_CONFIG;

typedef struct _HCI_ACL_PACKET_DATA
{
	u16					ACLDataPacketLen;
	u8					SyncDataPacketLen;
	u16					TotalNumACLDataPackets;
	u16					TotalSyncNumDataPackets;
} HCI_ACL_PACKET_DATA, *PHCI_ACL_PACKET_DATA;

typedef struct _HCI_PHY_LINK_BSS_INFO
{
	u16						bdCap;			// capability information

	// Qos related. Added by Annie, 2005-11-01.
//	BSS_QOS						BssQos; // not implement yet

} HCI_PHY_LINK_BSS_INFO, *PHCI_PHY_LINK_BSS_INFO;

typedef struct _PACKET_IRP_HCICMD_DATA
{
    u16		OCF:10;
    u16		OGF:6;
    u8		Length;
    u8		Data[1];
} PACKET_IRP_HCICMD_DATA, *PPACKET_IRP_HCICMD_DATA;

typedef struct _BT_ASOC_ENTRY
{
	u8						bUsed;
	u8						mAssoc;
	u8						b4waySuccess;
	u8						Bssid[6];
	HCI_PHY_LINK_CMD_DATA		PhyLinkCmdData;

	HCI_LOG_LINK_CMD_DATA		LogLinkCmdData[MAX_LOGICAL_LINK_NUM];

	HCI_ACL_PACKET_DATA			ACLPacketsData;

	AMP_ASSOC_CMD_DATA		AmpAsocCmdData;
	OCTET_STRING				BTSsid;
	u8						BTSsidBuf[33];

	HCI_STATUS						PhyLinkDisconnectReason;

	u8						bSendSupervisionPacket;
	//u8						CurrentSuervisionPacketSendNum;
	//u8						LastSuervisionPacketSendNum;
	u32						NoRxPktCnt;
	//Is Creator or Joiner
	AMP_BTAP_TYPE				AMPRole;

	//BT current state
	u8						BtCurrentState;
	//BT next state
	u8						BtNextState;

	u8						bNeedPhysLinkCompleteEvent;

	HCI_STATUS					PhysLinkCompleteStatus;

	u8						BTRemoteMACAddr[6];

	u32						BTCapability;

	u8						SyncDataPacketLen;

	u16						TotalSyncNumDataPackets;
	u16						TotalNumACLDataPackets;

	u8						ShortRangeMode;

	u8						PTK[PTK_LEN_TKIP];
	u8						GTK[GTK_LEN];
	u8						ANonce[KEY_NONCE_LEN];
	u8						SNonce[KEY_NONCE_LEN];
	u64						KeyReplayCounter;
	u8						WPAAuthReplayCount;
	u8						AESKeyBuf[AESCCMP_BLK_SIZE_TOTAL];
	u8						PMK[PMK_LEN];
	BT_STATE_WPA_AUTH			BTWPAAuthState;
	s32						UndecoratedSmoothedPWDB;

	// Add for HW security !!
	u8						HwCAMIndex;  // Cam index
	u8						bPeerQosSta;

	u32						rxSuvpPktCnt;
}BT_ASOC_ENTRY, *PBT_ASOC_ENTRY;

typedef struct _BT_TRAFFIC_STATISTICS
{
	u8				bTxBusyTraffic;
	u8				bRxBusyTraffic;
	u8				bIdle;
	u32				TxPktCntInPeriod;
	u32				RxPktCntInPeriod;
	u64				TxPktLenInPeriod;
	u64				RxPktLenInPeriod;
} BT_TRAFFIC_STATISTICS, *PBT_TRAFFIC_STATISTICS;

typedef struct _BT_MGNT
{
	u8				bBTConnectInProgress;
	u8				bLogLinkInProgress;
	u8				bPhyLinkInProgress;
	u8				bPhyLinkInProgressStartLL;
	u8				BtCurrentPhyLinkhandle;
	u16				BtCurrentLogLinkhandle;
	u8				CurrentConnectEntryNum;
	u8				DisconnectEntryNum;
	u8				CurrentBTConnectionCnt;
	BT_CONNECT_TYPE		BTCurrentConnectType;
	BT_CONNECT_TYPE		BTReceiveConnectPkt;
	u8				BTAuthCount;
	u8				BTAsocCount;
	u8				bStartSendSupervisionPkt;
	u8				BtOperationOn;
	u8				BTNeedAMPStatusChg;
	u8				JoinerNeedSendAuth;
	HCI_PHY_LINK_BSS_INFO	bssDesc;
	HCI_EXT_CONFIG		ExtConfig;
	u8				bNeedNotifyAMPNoCap;
	u8				bCreateSpportQos;
	u8				bSupportProfile;
	u8				BTChannel;
	u8				CheckChnlIsSuit;
	u8				bBtScan;
	u8				btLogoTest;
} BT_MGNT, *PBT_MGNT;

typedef struct _BT_HCI_DBG_INFO
{
	u32				hciCmdCnt;
	u32				hciCmdCntUnknown;
	u32				hciCmdCntCreatePhyLink;
	u32				hciCmdCntAcceptPhyLink;
	u32				hciCmdCntDisconnectPhyLink;
	u32				hciCmdPhyLinkStatus;
	u32				hciCmdCntCreateLogLink;
	u32				hciCmdCntAcceptLogLink;
	u32				hciCmdCntDisconnectLogLink;
	u32				hciCmdCntReadLocalAmpAssoc;
	u32				hciCmdCntWriteRemoteAmpAssoc;
	u32				hciCmdCntSetAclLinkStatus;
	u32				hciCmdCntSetScoLinkStatus;
	u32				hciCmdCntExtensionVersionNotify;
	u32				hciCmdCntLinkStatusNotify;
} BT_HCI_DBG_INFO, *PBT_HCI_DBG_INFO;

typedef struct _BT_IRP_DBG_INFO
{
	u32				irpMJCreate;
	// Io Control
	u32				irpIoControl;
	u32				irpIoCtrlHciCmd;
	u32				irpIoCtrlHciEvent;
	u32				irpIoCtrlHciTxData;
	u32				irpIoCtrlHciRxData;
	u32				irpIoCtrlUnknown;

	u32				irpIoCtrlHciTxData1s;
} BT_IRP_DBG_INFO, *PBT_IRP_DBG_INFO;

typedef struct _BT_PACKET_DBG_INFO
{
	u32				btPktTxProbReq;
	u32				btPktRxProbReq;
	u32				btPktRxProbReqFail;
	u32				btPktTxProbRsp;
	u32				btPktRxProbRsp;
	u32				btPktTxAuth;
	u32				btPktRxAuth;
	u32				btPktRxAuthButDrop;
	u32				btPktTxAssocReq;
	u32				btPktRxAssocReq;
	u32				btPktRxAssocReqButDrop;
	u32				btPktTxAssocRsp;
	u32				btPktRxAssocRsp;
	u32				btPktTxDisassoc;
	u32				btPktRxDisassoc;
	u32				btPktRxDeauth;
	u32				btPktTx4way1st;
	u32				btPktRx4way1st;
	u32				btPktTx4way2nd;
	u32				btPktRx4way2nd;
	u32				btPktTx4way3rd;
	u32				btPktRx4way3rd;
	u32				btPktTx4way4th;
	u32				btPktRx4way4th;
	u32				btPktTxLinkSuperReq;
	u32				btPktRxLinkSuperReq;
	u32				btPktTxLinkSuperRsp;
	u32				btPktRxLinkSuperRsp;
	u32				btPktTxData;
	u32				btPktRxData;
} BT_PACKET_DBG_INFO, *PBT_PACKET_DBG_INFO;

typedef struct _BT_DBG
{
	u8				dbgCtrl;
	u32				dbgProfile;
	BT_HCI_DBG_INFO		dbgHciInfo;
	BT_IRP_DBG_INFO		dbgIrpInfo;
	BT_PACKET_DBG_INFO	dbgBtPkt;
} BT_DBG, *PBT_DBG;

typedef struct _BT_HCI_INFO
{
	//802.11 Pal version specifier
	u8				BTPalVersion;
	u16				BTPalCompanyID;
	u16				BTPalsubversion;

	//Connected channel list
	u16				BTConnectChnlListLen;
	u8				BTConnectChnllist[64];

	//Fail contact counter
	u16				FailContactCount;

	//Event mask
	u64				BTEventMask;
	u64				BTEventMaskPage2;

	//timeout var
	u16				ConnAcceptTimeout;
	u16				LogicalAcceptTimeout;
	u16				PageTimeout;

	u8				LocationDomainAware;
	u16				LocationDomain;
	u8				LocationDomainOptions;
	u8				LocationOptions;

	u8				FlowControlMode;

	//Preferred channel list
	u16				BtPreChnlListLen;
	u8				BTPreChnllist[64];

	u16				enFlush_LLH;	//enhanced flush handle
	u16				FLTO_LLH;		//enhanced flush handle

	//==========================================
	//Test command only.
	u8				bInTestMode;
	u8				bTestIsEnd;
	u8				bTestNeedReport;
	u8				TestScenario;
	u8				TestReportInterval;
	u8				TestCtrType;
	u32				TestEventType;
	u16				TestNumOfFrame;
	u16				TestNumOfErrFrame;
	u16				TestNumOfBits;
	u16				TestNumOfErrBits;
	//==========================================
} BT_HCI_INFO, *PBT_HCI_INFO;

typedef struct _BT_TRAFFIC
{
	// Add for check replay data
	u8					LastRxUniFragNum;
	u16					LastRxUniSeqNum;

	//s32					EntryMaxUndecoratedSmoothedPWDB;
	//s32					EntryMinUndecoratedSmoothedPWDB;

	BT_TRAFFIC_STATISTICS		Bt30TrafficStatistics;
} BT_TRAFFIC, *PBT_TRAFFIC;

#define RT_WORK_ITEM _workitem
#define RT_THREAD _thread_hdl_

typedef struct _BT_SECURITY
{
	// WPA auth state
	// May need to remove to BTSecInfo ...
	//BT_STATE_WPA_AUTH BTWPAAuthState;
	//u8				PMK[PMK_LEN];
	RT_TIMER			BTWPAAuthTimer;
	OCTET_STRING		RSNIE;
	u8			RSNIEBuf[MAXRSNIELEN];
	u8			bRegNoEncrypt;
	u8			bUsedHwEncrypt;   // It is define by OS version !!
} BT_SECURITY, *PBT_SECURITY;

typedef struct _BT30Info
{
	PADAPTER			padapter;
	BT_ASOC_ENTRY		BtAsocEntry[MAX_BT_ASOC_ENTRY_NUM];
	BT_MGNT				BtMgnt;
	BT_DBG				BtDbg;
	BT_HCI_INFO			BtHciInfo;
	BT_TRAFFIC			BtTraffic;
	BT_SECURITY			BtSec;

#if(BT_THREAD == 0)
	RT_WORK_ITEM		HCICmdWorkItem;
	RT_TIMER				BTHCICmdTimer;
#endif
#if (SENDTXMEHTOD==0)
	RT_WORK_ITEM		HCISendACLDataWorkItem;
	RT_TIMER				BTHCISendAclDataTimer;
#elif(SENDTXMEHTOD==2)
	RT_THREAD			BTTxThread;
#endif
	RT_WORK_ITEM		BTPsDisableWorkItem;
	RT_WORK_ITEM		BTConnectWorkItem;
	RT_TIMER				BTHCIDiscardAclDataTimer;
	RT_TIMER				BTHCIJoinTimeoutTimer;
	RT_TIMER				BTTestSendPacketTimer;
	RT_TIMER				BTSupervisionPktTimer;
	RT_TIMER				BTDisconnectPhyLinkTimer;
	RT_TIMER				BTBeaconTimer;
	u8				BTBeaconTmrOn;

	RT_TIMER				BTPsDisableTimer;
	RT_TIMER				BTAuthTimeoutTimer;
	RT_TIMER				BTAsocTimeoutTimer;

	PVOID				pBtChnlList;
}BT30Info, *PBT30Info;

typedef struct _PACKET_IRP_ACL_DATA
{
    u16		Handle:12;
    u16		PB_Flag:2;
    u16		BC_Flag:2;
    u16		Length;
    u8		Data[1];
} PACKET_IRP_ACL_DATA, *PPACKET_IRP_ACL_DATA;

typedef struct _PACKET_IRP_HCIEVENT_DATA
{
    u8		EventCode;
    u8		Length;
    u8		Data[1];
} PACKET_IRP_HCIEVENT_DATA, *PPACKET_IRP_HCIEVENT_DATA;

typedef struct _COMMON_TRIPLE
{
	u8 byte_1st;
	u8 byte_2nd;
	u8 byte_3rd;
} COMMON_TRIPLE, *PCOMMON_TRIPLE;

#define COUNTRY_STR_LEN		3	// country string len=3

#define LOCAL_PMK	0

typedef enum _HCI_WIFI_CONNECT_STATUS
{
	HCI_WIFI_NOT_CONNECTED				=0x0,
	HCI_WIFI_CONNECTED					=0x1,
	HCI_WIFI_CONNECT_IN_PROGRESS		=0x2,
} HCI_WIFI_CONNECT_STATUS, *PHCI_WIFI_CONNECT_STATUS;

typedef enum _HCI_EXT_BT_OPERATION
{
	HCI_BT_OP_NONE				= 0x0,
	HCI_BT_OP_INQUIRY_START		= 0x1,
	HCI_BT_OP_INQUIRY_FINISH		= 0x2,
	HCI_BT_OP_PAGING_START		= 0x3,
	HCI_BT_OP_PAGING_SUCCESS		= 0x4,
	HCI_BT_OP_PAGING_UNSUCCESS	= 0x5,
	HCI_BT_OP_PAIRING_START		= 0x6,
	HCI_BT_OP_PAIRING_FINISH		= 0x7,
	HCI_BT_OP_BT_DEV_ENABLE		= 0x8,
	HCI_BT_OP_BT_DEV_DISABLE		= 0x9,
	HCI_BT_OP_MAX
} HCI_EXT_BT_OPERATION, *PHCI_EXT_BT_OPERATION;

//======================================
//	Function proto type
//======================================
#define RT_LIST_ENTRY _list
typedef struct _BTData_ENTRY
{
	RT_LIST_ENTRY	List;
	void			*pDataBlock;
} BTData_ENTRY, *PBTData_ENTRY;

#define BTHCI_SM_WITH_INFO(_Adapter, _StateToEnter, _StateCmd, _EntryNum)	\
{										\
	RTPRINT(FIOCTL, IOCTL_STATE, ("[BT state change] caused by ""%s"", line=%d\n", __FUNCTION__, __LINE__));							\
	BTHCI_StateMachine(_Adapter, _StateToEnter, _StateCmd, _EntryNum);\
}

void BTHCI_EventParse(PADAPTER padapter, void *pEvntData, u32 dataLen);
#define BT_EventParse BTHCI_EventParse
u8 BTHCI_HsConnectionEstablished(PADAPTER padapter);
void BTHCI_UpdateBTProfileRTKToMoto(PADAPTER padapter);
void BTHCI_WifiScanNotify(PADAPTER padapter, u8 scanType);
void BTHCI_StateMachine(PADAPTER padapter, u8 StateToEnter, HCI_STATE_WITH_CMD StateCmd, u8 EntryNum);
void BTHCI_DisconnectPeer(PADAPTER padapter, u8 EntryNum);
void BTHCI_EventNumOfCompletedDataBlocks(PADAPTER padapter);
void BTHCI_EventAMPStatusChange(PADAPTER padapter, u8 AMP_Status);
void BTHCI_DisconnectAll(PADAPTER padapter);
HCI_STATUS BTHCI_HandleHCICMD(PADAPTER padapter, PPACKET_IRP_HCICMD_DATA pHciCmd);

// ===== End of sync from SD7 driver COMMON/bt_hci.h =====
#endif // __BT_HCI_C__

#ifdef __HALBTC87231ANT_C__ // HAL/BTCoexist/HalBtc87231Ant.h
// ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtc87231Ant.h =====

#define	BTC_FOR_SCAN_START				1
#define	BTC_FOR_SCAN_FINISH				0

#define	BT_TXRX_CNT_THRES_1				1200
#define	BT_TXRX_CNT_THRES_2				1400
#define	BT_TXRX_CNT_THRES_3				3000
#define	BT_TXRX_CNT_LEVEL_0				0	// < 1200
#define	BT_TXRX_CNT_LEVEL_1				1	// >= 1200 && < 1400
#define	BT_TXRX_CNT_LEVEL_2				2	// >= 1400
#define	BT_TXRX_CNT_LEVEL_3				3	// >= 3000

typedef enum _BT_STATE_1ANT{
	BT_INFO_STATE_DISABLED			= 0,
	BT_INFO_STATE_NO_CONNECTION		= 1,
	BT_INFO_STATE_CONNECT_IDLE		= 2,
	BT_INFO_STATE_INQ_OR_PAG		= 3,
	BT_INFO_STATE_ACL_ONLY_BUSY		= 4,
	BT_INFO_STATE_SCO_ONLY_BUSY		= 5,
	BT_INFO_STATE_ACL_SCO_BUSY		= 6,
	BT_INFO_STATE_HID					= 7,
	BT_INFO_STATE_A2DP				= 8,
	BT_INFO_STATE_HIDA2DP			= 9,
	BT_INFO_STATE_PANA2DP			= 10,
	BT_INFO_STATE_MAX				= 11
} BT_STATE_1ANT, *PBT_STATE_1ANT;

typedef struct _BTDM_8723A_1ANT
{
	u8		prePsTdma;
	u8		curPsTdma;
	u8		psTdmaDuAdjType;
	u8		bPrePsTdmaOn;
	u8		bCurPsTdmaOn;
	u8		preWifiPara;
	u8		curWifiPara;
	u8		preCoexWifiCon;
	u8		curCoexWifiCon;
	u8		wifiRssiThresh;

	u32		psTdmaMonitorCnt;
	u32		psTdmaGlobalCnt;

	u8		bWiFiHalt;
} BTDM_8723A_1ANT, *PBTDM_8723A_1ANT;

void BTDM_1AntBtCoexist8723A(PADAPTER padapter);

// ===== End of sync from SD7 driver HAL/BTCoexist/HalBtc87231Ant.h =====
#endif // __HALBTC87231ANT_C__

#ifdef __HALBTC87232ANT_C__ // HAL/BTCoexist/HalBtc87232Ant.h
// ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtc87232Ant.h =====

typedef struct _BTDM_8723A_2ANT
{
	u8		bAllOff;
	u8		bAgcTableEn;
	u8		bAdcBackOffOn;

	u8		b2AntHidEn;
	u8		bLowPenaltyRateAdaptive;
	u8		bRfRxLpfShrink;
	u8		bRejectAggrePkt;

	u8		bTdmaOn;
	u8		tdmaAnt;
	u8		tdmaNav;
	u8		tdmaDacSwing;
	u8		fwDacSwingLvl;
	// Traditional TDMA
	u8		bTraTdmaOn;
	u8		traTdmaAnt;
	u8		traTdmaNav;
	// PS TDMA
	u8		bPsTdmaOn;
	u8		psTdmaByte[5];
	u8		bIgnoreWlanAct;

	u8		bPtaOn;
	u32		val0x6c0;
	u32		val0x6c8;
	u8		val0x6cc;

	u8		bSwDacSwingOn;
	u32		swDacSwingLvl;
	u8		wlanActHi;
	u8		wlanActLo;
	u8		btRetryIndex;

	u8		bDecBtPwr;
	u8		bInqCnt;
} BTDM_8723A_2ANT, *PBTDM_8723A_2ANT;
void BTDM_2AntBtCoexist8723A(PADAPTER padapter);
// ===== End of sync from SD7 driver HAL/BTCoexist/HalBtc87232Ant.h =====
#endif // __HALBTC87232ANT_C__

#ifdef __HALBTC8723_C__ // HAL/BTCoexist/HalBtc8723.h

// HEADER/PlatformDef.h
typedef enum _RT_MEDIA_STATUS {
	RT_MEDIA_DISCONNECT = 0,
	RT_MEDIA_CONNECT       = 1
} RT_MEDIA_STATUS;

// ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtc8723.h =====

#define	BT_Q_PKT_OFF		0
#define	BT_Q_PKT_ON		1

#define	BT_TX_PWR_OFF		0
#define	BT_TX_PWR_ON		1

// TDMA mode definition
#define	TDMA_2ANT			0
#define	TDMA_1ANT			1
#define	TDMA_NAV_OFF		0
#define	TDMA_NAV_ON		1
#define	TDMA_DAC_SWING_OFF	0
#define	TDMA_DAC_SWING_ON	1

#define	BT_RSSI_LEVEL_H	0
#define	BT_RSSI_LEVEL_M	1
#define	BT_RSSI_LEVEL_L	2

// PTA mode related definition
#define	BT_PTA_MODE_OFF		0
#define	BT_PTA_MODE_ON		1

// Penalty Tx Rate Adaptive
#define	BT_TX_RATE_ADAPTIVE_NORMAL			0
#define	BT_TX_RATE_ADAPTIVE_LOW_PENALTY	1

// RF Corner
#define	BT_RF_RX_LPF_CORNER_RESUME			0
#define	BT_RF_RX_LPF_CORNER_SHRINK			1

#define	BTINFO_B_FTP					BIT(7)
#define	BTINFO_B_A2DP					BIT(6)
#define	BTINFO_B_HID					BIT(5)
#define	BTINFO_B_SCO_BUSY				BIT(4)
#define	BTINFO_B_ACL_BUSY				BIT(3)
#define	BTINFO_B_INQ_PAGE				BIT(2)
#define	BTINFO_B_SCO_ESCO				BIT(1)
#define	BTINFO_B_CONNECTION				BIT(0)

typedef struct _BT_COEXIST_8723A
{
	u32					highPriorityTx;
	u32					highPriorityRx;
	u32					lowPriorityTx;
	u32					lowPriorityRx;
	u8					btRssi;
	u8					TotalAntNum;
	u8					bC2hBtInfoSupport;
	u8					c2hBtInfo;
	u8					c2hBtInfoOriginal;
	u8					prec2hBtInfo; // for 1Ant
	u8					bC2hBtInquiryPage;
//	u8					btInquiryPageCnt;
	u64					btInqPageStartTime; // for 2Ant
	u8					c2hBtProfile; // for 1Ant
	u8					btRetryCnt;
	u8					bC2hBtInfoReqSent;
	u8					bForceFwBtInfo;
	u8					bForceA2dpSink;
//	u8					bForceLps;
//	u8					bBtPwrSaveMode;
	BTDM_8723A_2ANT			btdm2Ant;
	BTDM_8723A_1ANT			btdm1Ant;
} BT_COEXIST_8723A, *PBT_COEXIST_8723A;

void BTDM_SetFwChnlInfo(PADAPTER padapter, RT_MEDIA_STATUS mstatus);
u8 BTDM_IsWifiConnectionExist(PADAPTER padapter);
void BTDM_SetFw3a(PADAPTER padapter, u8 byte1, u8 byte2, u8 byte3, u8 byte4, u8 byte5);
void BTDM_QueryBtInformation(PADAPTER padapter);
void BTDM_SetSwRfRxLpfCorner(PADAPTER padapter, u8 type);
void BTDM_SetSwPenaltyTxRateAdaptive(PADAPTER padapter, u8 raType);
void BTDM_SetFwDecBtPwr(PADAPTER padapter, u8 bDecBtPwr);
u8 BTDM_BtProfileSupport(PADAPTER padapter);
u8 BTDM_1Ant8723A(PADAPTER padapter);
#define BT_1Ant BTDM_1Ant8723A

// ===== End of sync from SD7 driver HAL/BTCoexist/HalBtc8723.h =====
#endif // __HALBTC8723_C__

#ifdef __HALBTCCSR1ANT_C__ // HAL/BTCoexist/HalBtcCsr1Ant.h
// ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtcCsr1Ant.h =====

enum BT_A2DP_INDEX{
	BT_A2DP_INDEX0		=0,			// 32,12; the most critical for BT
	BT_A2DP_INDEX1,					// 12,24
	BT_A2DP_INDEX2,					// 0,0
	BT_A2DP_INDEX_MAX
};

#define BT_A2DP_STATE_NOT_ENTERED		0
#define BT_A2DP_STATE_DETECTING		1
#define BT_A2DP_STATE_DETECTED			2

#define BTDM_ANT_BT_IDLE				0
#define BTDM_ANT_WIFI					1
#define BTDM_ANT_BT						2


void BTDM_SingleAnt(PADAPTER padapter, u8 bSingleAntOn, u8 bInterruptOn, u8 bMultiNAVOn);
void BTDM_CheckBTIdleChange1Ant(PADAPTER padapter);

// ===== End of sync from SD7 driver HAL/BTCoexist/HalBtcCsr1Ant.h =====
#endif // __HALBTCCSR1ANT_C__

#ifdef __HALBTCCSR2ANT_C__ // HAL/BTCoexist/HalBtcCsr2Ant.h
// ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtcCsr2Ant.h =====

//===========================================
// For old core stack before v251
//===========================================
#define BT_RSSI_STATE_NORMAL_POWER	BIT0
#define BT_RSSI_STATE_AMDPU_OFF		BIT1
#define BT_RSSI_STATE_SPECIAL_LOW	BIT2
#define BT_RSSI_STATE_BG_EDCA_LOW	BIT3
#define BT_RSSI_STATE_TXPOWER_LOW	BIT4

#define	BT_DACSWING_OFF				0
#define	BT_DACSWING_M4				1
#define	BT_DACSWING_M7				2
#define	BT_DACSWING_M10				3

void BTDM_DiminishWiFi(PADAPTER Adapter, u8 bDACOn, u8 bInterruptOn, u8 DACSwingLevel, u8 bNAVOn);

// ===== End of sync from SD7 driver HAL/BTCoexist/HalBtcCsr2Ant.h =====
#endif // __HALBTCCSR2ANT_C__

#ifdef __HALBTCOEXIST_C__ // HAL/BTCoexist/HalBtCoexist.h

// HEADER/TypeDef.h
#define MAX_FW_SUPPORT_MACID_NUM			64

// ===== Below this line is sync from SD7 driver HAL/BTCoexist/HalBtCoexist.h =====

#define	FW_VER_BT_REG			62
#define	FW_VER_BT_REG1		74
#define	REG_BT_ACTIVE			0x444
#define	REG_BT_STATE			0x448
#define	REG_BT_POLLING1		0x44c
#define	REG_BT_POLLING			0x700

#define	REG_BT_ACTIVE_OLD		0x488
#define	REG_BT_STATE_OLD		0x48c
#define	REG_BT_POLLING_OLD	0x490

// The reg define is for 8723
#define	REG_HIGH_PRIORITY_TXRX			0x770
#define	REG_LOW_PRIORITY_TXRX			0x774

#define BT_FW_COEX_THRESH_TOL			6
#define BT_FW_COEX_THRESH_20				20
#define BT_FW_COEX_THRESH_23				23
#define BT_FW_COEX_THRESH_25				25
#define BT_FW_COEX_THRESH_30				30
#define BT_FW_COEX_THRESH_35				35
#define BT_FW_COEX_THRESH_40				40
#define BT_FW_COEX_THRESH_45				45
#define BT_FW_COEX_THRESH_47				47
#define BT_FW_COEX_THRESH_50				50
#define BT_FW_COEX_THRESH_55				55
#define BT_FW_COEX_THRESH_65				65

#define BT_COEX_STATE_BT30					BIT(0)
#define BT_COEX_STATE_WIFI_HT20			BIT(1)
#define BT_COEX_STATE_WIFI_HT40			BIT(2)
#define BT_COEX_STATE_WIFI_LEGACY			BIT(3)

#define BT_COEX_STATE_WIFI_RSSI_LOW		BIT(4)
#define BT_COEX_STATE_WIFI_RSSI_MEDIUM	BIT(5)
#define BT_COEX_STATE_WIFI_RSSI_HIGH		BIT(6)
#define BT_COEX_STATE_DEC_BT_POWER		BIT(7)

#define BT_COEX_STATE_WIFI_IDLE			BIT(8)
#define BT_COEX_STATE_WIFI_UPLINK			BIT(9)
#define BT_COEX_STATE_WIFI_DOWNLINK		BIT(10)

#define BT_COEX_STATE_BT_INQ_PAGE	BIT(11)
#define BT_COEX_STATE_BT_IDLE				BIT(12)
#define BT_COEX_STATE_BT_UPLINK			BIT(13)
#define BT_COEX_STATE_BT_DOWNLINK		BIT(14)
//===========================================
// Todo: Remove these definitions
#define BT_COEX_STATE_BT_PAN_IDLE			BIT(15)
#define BT_COEX_STATE_BT_PAN_UPLINK		BIT(16)
#define BT_COEX_STATE_BT_PAN_DOWNLINK	BIT(17)
#define BT_COEX_STATE_BT_A2DP_IDLE		BIT(18)
//===========================================
#define BT_COEX_STATE_BT_RSSI_LOW			BIT(19)

#define BT_COEX_STATE_PROFILE_HID			BIT(20)
#define BT_COEX_STATE_PROFILE_A2DP		BIT(21)
#define BT_COEX_STATE_PROFILE_PAN			BIT(22)
#define BT_COEX_STATE_PROFILE_SCO			BIT(23)

#define BT_COEX_STATE_WIFI_RSSI_1_LOW		BIT(24)
#define BT_COEX_STATE_WIFI_RSSI_1_MEDIUM	BIT(25)
#define BT_COEX_STATE_WIFI_RSSI_1_HIGH		BIT(26)

#define BT_COEX_STATE_WIFI_RSSI_BEACON_LOW		BIT(27)
#define BT_COEX_STATE_WIFI_RSSI_BEACON_MEDIUM		BIT(28)
#define BT_COEX_STATE_WIFI_RSSI_BEACON_HIGH		BIT(29)


#define BT_COEX_STATE_BTINFO_COMMON				BIT30
#define BT_COEX_STATE_BTINFO_B_HID_SCOESCO		BIT31
#define BT_COEX_STATE_BTINFO_B_FTP_A2DP			BIT32

#define BT_COEX_STATE_BT_CNT_LEVEL_0				BIT33
#define BT_COEX_STATE_BT_CNT_LEVEL_1				BIT34
#define BT_COEX_STATE_BT_CNT_LEVEL_2				BIT35
#define BT_COEX_STATE_BT_CNT_LEVEL_3				BIT36

#define BT_RSSI_STATE_HIGH					0
#define BT_RSSI_STATE_MEDIUM				1
#define BT_RSSI_STATE_LOW					2
#define BT_RSSI_STATE_STAY_HIGH			3
#define BT_RSSI_STATE_STAY_MEDIUM			4
#define BT_RSSI_STATE_STAY_LOW			5



#define	BT_AGCTABLE_OFF				0
#define	BT_AGCTABLE_ON				1

#define	BT_BB_BACKOFF_OFF			0
#define	BT_BB_BACKOFF_ON				1

#define	BT_FW_NAV_OFF					0
#define	BT_FW_NAV_ON					1


#define	BT_COEX_MECH_NONE				0
#define	BT_COEX_MECH_SCO				1
#define	BT_COEX_MECH_HID				2
#define	BT_COEX_MECH_A2DP				3
#define	BT_COEX_MECH_PAN				4
#define	BT_COEX_MECH_HID_A2DP			5
#define	BT_COEX_MECH_HID_PAN			6
#define	BT_COEX_MECH_PAN_A2DP			7
#define	BT_COEX_MECH_HID_SCO_ESCO		8
#define	BT_COEX_MECH_FTP_A2DP			9
#define	BT_COEX_MECH_COMMON			10
#define	BT_COEX_MECH_MAX				11
//===========================================
//	BT Dbg Ctrl
//===========================================
#define	BT_DBG_PROFILE_NONE			0
#define	BT_DBG_PROFILE_SCO			1
#define	BT_DBG_PROFILE_HID			2
#define	BT_DBG_PROFILE_A2DP			3
#define	BT_DBG_PROFILE_PAN			4
#define	BT_DBG_PROFILE_HID_A2DP		5
#define	BT_DBG_PROFILE_HID_PAN		6
#define	BT_DBG_PROFILE_PAN_A2DP		7
#define	BT_DBG_PROFILE_MAX			9
//===========================================

typedef struct _BT_COEXIST_STR
{
	u8					BluetoothCoexist;
	u8					BT_Ant_Num;
	u8					BT_CoexistType;
	u8					BT_Ant_isolation;	//0:good, 1:bad
	u8					BT_RadioSharedType;
	u32					Ratio_Tx;
	u32					Ratio_PRI;
	u8					bInitlized;
	u32					BtRfRegOrigin1E;
	u32					BtRfRegOrigin1F;
	u8					bBTBusyTraffic;
	u8					bBTTrafficModeSet;
	u8					bBTNonTrafficModeSet;
	BT_TRAFFIC_STATISTICS		BT21TrafficStatistics;
	u64					CurrentState;
	u64					PreviousState;
	u8					preRssiState;
	u8					preRssiState1;
	u8					preRssiStateBeacon;
	u8					bFWCoexistAllOff;
	u8					bSWCoexistAllOff;
	u8					bHWCoexistAllOff;
	u8					bBalanceOn;
	u8					bSingleAntOn;
	u8					bInterruptOn;
	u8					bMultiNAVOn;
	u8					PreWLANActH;
	u8					PreWLANActL;
	u8					WLANActH;
	u8					WLANActL;
	u8					A2DPState;
	u8					AntennaState;
	u32					lastBtEdca;
	u16					last_aggr_num;
	u8					bEDCAInitialized;
	u8					exec_cnt;
	u8					b8723aAgcTableOn;
	u8					b92DAgcTableOn;
	BT_COEXIST_8723A			halCoex8723;
	u8					btActiveZeroCnt;
	u8					bCurBtDisabled;
	u8					bPreBtDisabled;
	u8					bNeedToRoamForBtDisableEnable;
}BT_COEXIST_STR, *PBT_COEXIST_STR;


void BTDM_CheckAntSelMode(PADAPTER padapter);
void BTDM_FwC2hBtRssi(PADAPTER padapter, u8 *tmpBuf);
#define BT_FwC2hBtRssi BTDM_FwC2hBtRssi
void BTDM_FwC2hBtInfo(PADAPTER padapter, u8 *tmpBuf, u8 length);
#define BT_FwC2hBtInfo BTDM_FwC2hBtInfo
void BTDM_DisplayBtCoexInfo(PADAPTER padapter);
#define BT_DisplayBtCoexInfo BTDM_DisplayBtCoexInfo
void BTDM_RejectAPAggregatedPacket(PADAPTER padapter, u8 bReject);
u8 BTDM_IsHT40(PADAPTER padapter);
u8 BTDM_Legacy(PADAPTER padapter);
void BTDM_CheckWiFiState(PADAPTER padapter);
s32 BTDM_GetRxSS(PADAPTER padapter);
u8 BTDM_CheckCoexBcnRssiState(PADAPTER padapter, u8 levelNum, u8 RssiThresh, u8 RssiThresh1);
u8 BTDM_CheckCoexRSSIState1(PADAPTER padapter, u8 levelNum, u8 RssiThresh, u8 RssiThresh1);
u8 BTDM_CheckCoexRSSIState(PADAPTER padapter, u8 levelNum, u8 RssiThresh, u8 RssiThresh1);
u8 BTDM_DisableEDCATurbo(PADAPTER padapter);
#define BT_DisableEDCATurbo BTDM_DisableEDCATurbo
void BTDM_Balance(PADAPTER padapter, u8 bBalanceOn, u8 ms0, u8 ms1);
void BTDM_AGCTable(PADAPTER padapter, u8 type);
void BTDM_BBBackOffLevel(PADAPTER padapter, u8 type);
void BTDM_FWCoexAllOff(PADAPTER padapter);
void BTDM_SWCoexAllOff(PADAPTER padapter);
void BTDM_HWCoexAllOff(PADAPTER padapter);
void BTDM_CoexAllOff(PADAPTER padapter);
void BTDM_TurnOffBtCoexistBeforeEnterIPS(PADAPTER padapter);
void BTDM_Coexist(PADAPTER padapter);
#define BT_CoexistMechanism BTDM_Coexist
void BTDM_UpdateCoexState(PADAPTER padapter);
u8 BTDM_IsSameCoexistState(PADAPTER padapter);
void BTDM_PWDBMonitor(PADAPTER padapter);
u8 BTDM_IsBTBusy(PADAPTER padapter);
#define BT_IsBtBusy BTDM_IsBTBusy
u8 BTDM_IsWifiBusy(PADAPTER padapter);
u8 BTDM_IsCoexistStateChanged(PADAPTER padapter);
u8 BTDM_IsWifiUplink(PADAPTER padapter);
u8 BTDM_IsWifiDownlink(PADAPTER padapter);
u8 BTDM_IsBTHSMode(PADAPTER padapter);
u8 BTDM_IsBTUplink(PADAPTER padapter);
u8 BTDM_IsBTDownlink(PADAPTER padapter);
void BTDM_AdjustForBtOperation(PADAPTER padapter);
void BTDM_ForHalt(PADAPTER padapter);
#define BT_HaltProcess BTDM_ForHalt
void BTDM_WifiScanNotify(PADAPTER padapter, u8 scanType);
void BTDM_WifiAssociateNotify(PADAPTER padapter, u8 action);
void BTDM_MediaStatusNotify(PADAPTER padapter, RT_MEDIA_STATUS mstatus);
#define BT_WifiMediaStatusNotify BTDM_MediaStatusNotify
void BTDM_ForDhcp(PADAPTER padapter);
#define BT_SpecialPacket BTDM_ForDhcp
void BTDM_ResetActionProfileState(PADAPTER padapter);
void BTDM_SetBtCoexCurrAntNum(PADAPTER padapter, u8 antNum);
#define BT_SetBtCoexCurrAntNum BTDM_SetBtCoexCurrAntNum
u8 BTDM_IsActionSCO(PADAPTER padapter);
u8 BTDM_IsActionHID(PADAPTER padapter);
u8 BTDM_IsActionA2DP(PADAPTER padapter);
u8 BTDM_IsActionPAN(PADAPTER padapter);
u8 BTDM_IsActionHIDA2DP(PADAPTER padapter);
u8 BTDM_IsActionHIDPAN(PADAPTER padapter);
u8 BTDM_IsActionPANA2DP(PADAPTER padapter);
u8 BTDM_IsBtDisabled(PADAPTER padapter);
#define BT_IsBtDisabled BTDM_IsBtDisabled
u32 BTDM_BtTxRxCounterH(PADAPTER padapter);
u32 BTDM_BtTxRxCounterL(PADAPTER padapter);

// ===== End of sync from SD7 driver HAL/BTCoexist/HalBtCoexist.h =====
#endif // __HALBTCOEXIST_C__

#ifdef __HALBT_C__ // HAL/HalBT.h
// ===== Below this line is sync from SD7 driver HAL/HalBT.h =====

#define RTS_CTS_NO_LEN_LIMIT	0

u8 HALBT_GetPGAntNum(PADAPTER padapter);
#define BT_GetPGAntNum HALBT_GetPGAntNum
void HALBT_SetKey(PADAPTER padapter, u8 EntryNum);
void HALBT_RemoveKey(PADAPTER padapter, u8 EntryNum);
void HALBT_InitBTVars8723A(PADAPTER padapter);
#define HALBT_InitHalVars HALBT_InitBTVars8723A
#define BT_InitHalVars HALBT_InitHalVars
u8 HALBT_IsBTExist(PADAPTER padapter);
#define BT_IsBtExist HALBT_IsBTExist
u8 HALBT_BTChipType(PADAPTER padapter);
void HALBT_InitHwConfig(PADAPTER padapter);
#define BT_InitHwConfig HALBT_InitHwConfig
void HALBT_SetRtsCtsNoLenLimit(PADAPTER padapter);

// ===== End of sync from SD7 driver HAL/HalBT.c =====
#endif // __HALBT_C__

#endif // __RTL8723A_BT_COEXIST_H__

