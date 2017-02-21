/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
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
#ifndef __RTW_BTCOEX_H__
#define __RTW_BTCOEX_H__

#include <drv_types.h>

/* For H2C: H2C_BT_MP_OPER. Return status definition to the user layer */
typedef enum _BT_CTRL_STATUS {
	BT_STATUS_SUCCESS								= 0x00, /* Success */
	BT_STATUS_BT_OP_SUCCESS							= 0x01, /* bt fw op execution success */
	BT_STATUS_H2C_SUCCESS							= 0x02, /* H2c success */
	BT_STATUS_H2C_FAIL								= 0x03, /* H2c fail */
	BT_STATUS_H2C_LENGTH_EXCEEDED					= 0x04, /* H2c command length exceeded */
	BT_STATUS_H2C_TIMTOUT							= 0x05, /* H2c timeout */
	BT_STATUS_H2C_BT_NO_RSP							= 0x06, /* H2c sent, bt no rsp */
	BT_STATUS_C2H_SUCCESS							= 0x07, /* C2h success */
	BT_STATUS_C2H_REQNUM_MISMATCH					= 0x08, /* bt fw wrong rsp */
	BT_STATUS_OPCODE_U_VERSION_MISMATCH				= 0x08, /* Upper layer OP code version mismatch. */
	BT_STATUS_OPCODE_L_VERSION_MISMATCH				= 0x0a, /* Lower layer OP code version mismatch. */
	BT_STATUS_UNKNOWN_OPCODE_U						= 0x0b, /* Unknown Upper layer OP code */
	BT_STATUS_UNKNOWN_OPCODE_L						= 0x0c, /* Unknown Lower layer OP code */
	BT_STATUS_PARAMETER_FORMAT_ERROR_U				= 0x0d, /* Wrong parameters sent by upper layer. */
	BT_STATUS_PARAMETER_FORMAT_ERROR_L				= 0x0e, /* bt fw parameter format is not consistency */
	BT_STATUS_PARAMETER_OUT_OF_RANGE_U				= 0x0f, /* uppery layer parameter value is out of range */
	BT_STATUS_PARAMETER_OUT_OF_RANGE_L				= 0x10, /* bt fw parameter value is out of range */
	BT_STATUS_UNKNOWN_STATUS_L						= 0x11, /* bt returned an defined status code */
	BT_STATUS_UNKNOWN_STATUS_H						= 0x12, /* driver need to do error handle or not handle-well. */
	BT_STATUS_WRONG_LEVEL							= 0x13, /* should be under passive level */
	BT_STATUS_NOT_IMPLEMENT						= 0x14, /* op code not implemented yet */
	BT_STATUS_BT_STACK_OP_SUCCESS					= 0x15, /* bt stack op execution success */
	BT_STATUS_BT_STACK_NOT_SUPPORT					= 0x16, /* stack version not support this. */
	BT_STATUS_BT_STACK_SEND_HCI_EVENT_FAIL			= 0x17, /* send hci event fail */
	BT_STATUS_BT_STACK_NOT_BIND						= 0x18, /* stack not bind wifi driver */
	BT_STATUS_BT_STACK_NO_RSP						= 0x19, /* stack doesn't have any rsp. */
	BT_STATUS_MAX
} BT_CTRL_STATUS, *PBT_CTRL_STATUS;

#define SET_BT_MP_OPER_RET(OpCode, StatusCode)						((OpCode << 8) | StatusCode)
#define GET_OP_CODE_FROM_BT_MP_OPER_RET(RetCode)					((RetCode & 0xF0) >> 8)
#define GET_STATUS_CODE_FROM_BT_MP_OPER_RET(RetCode)				(RetCode & 0x0F)
#define CHECK_STATUS_CODE_FROM_BT_MP_OPER_RET(RetCode, StatusCode)	(GET_STATUS_CODE_FROM_BT_MP_OPER_RET(RetCode) == StatusCode)

#ifdef CONFIG_BT_COEXIST_SOCKET_TRX

#define NETLINK_USER 31
#define CONNECT_PORT 30000
#define CONNECT_PORT_BT 30001
#define KERNEL_SOCKET_OK 0x01	
#define NETLINK_SOCKET_OK 0x02

#define OTHER 0
#define RX_ATTEND_ACK 1
#define RX_LEAVE_ACK 2
#define RX_BT_LEAVE 3
#define RX_INVITE_REQ 4
#define RX_ATTEND_REQ 5
#define RX_INVITE_RSP 6

#define invite_req "INVITE_REQ"
#define invite_rsp "INVITE_RSP"
#define attend_req "ATTEND_REQ"
#define attend_ack "ATTEND_ACK"
#define wifi_leave "WIFI_LEAVE"
#define leave_ack "LEAVE_ACK"
#define bt_leave "BT_LEAVE"

#define BT_INFO_NOTIFY_CMD 0x0106
#define BT_INFO_LEN 8

typedef struct _HCI_LINK_INFO{
	u2Byte					ConnectHandle;
	u1Byte					IncomingTrafficMode;
	u1Byte					OutgoingTrafficMode;
	u1Byte					BTProfile;
	u1Byte					BTCoreSpec;
	s1Byte					BT_RSSI;
	u1Byte					TrafficProfile;
	u1Byte					linkRole;
}HCI_LINK_INFO, *PHCI_LINK_INFO;

#define	MAX_BT_ACL_LINK_NUM				8

typedef struct _HCI_EXT_CONFIG{
	HCI_LINK_INFO				aclLink[MAX_BT_ACL_LINK_NUM];
	u1Byte					btOperationCode;
	u2Byte					CurrentConnectHandle;
	u1Byte					CurrentIncomingTrafficMode;
	u1Byte					CurrentOutgoingTrafficMode;

	u1Byte					NumberOfACL;
	u1Byte					NumberOfSCO;
	u1Byte					CurrentBTStatus;
	u2Byte					HCIExtensionVer;

	BOOLEAN					bEnableWifiScanNotify;
}HCI_EXT_CONFIG, *PHCI_EXT_CONFIG;

typedef struct _HCI_PHY_LINK_BSS_INFO{
	u2Byte						bdCap;			// capability information

	// Qos related. Added by Annie, 2005-11-01.
	//BSS_QOS						BssQos;		
	
}HCI_PHY_LINK_BSS_INFO, *PHCI_PHY_LINK_BSS_INFO;

typedef enum _BT_CONNECT_TYPE{
	BT_CONNECT_AUTH_REQ								=0x00,	
	BT_CONNECT_AUTH_RSP								=0x01,
	BT_CONNECT_ASOC_REQ								=0x02,
	BT_CONNECT_ASOC_RSP								=0x03,
	BT_DISCONNECT										=0x04
}BT_CONNECT_TYPE,*PBT_CONNECT_TYPE;


typedef struct _PACKET_IRP_HCIEVENT_DATA {
	    u8		EventCode;
	    u8		Length; //total cmd length = extension event length+1(extension event code length)
	    u8		Data[1]; // byte1 is extension event code
} rtw_HCI_event;


struct btinfo_8761ATV {
	u8 cid;
	u8 len;

	u8 bConnection:1;
	u8 bSCOeSCO:1;
	u8 bInQPage:1;
	u8 bACLBusy:1;
	u8 bSCOBusy:1;
	u8 bHID:1;
	u8 bA2DP:1;
	u8 bFTP:1;

	u8 retry_cnt:4;
	u8 rsvd_34:1;
	u8 bPage:1;
	u8 TRxMask:1;
	u8 Sniff_attempt:1;

	u8 rssi;

	u8 A2dp_rate:1;
	u8 ReInit:1;
	u8 MaxPower:1;
	u8 bEnIgnoreWlanAct:1;
	u8 TxPowerLow:1;
	u8 TxPowerHigh:1;
	u8 eSCO_SCO:1;
	u8 Master_Slave:1;

	u8 ACL_TRx_TP_low;
	u8 ACL_TRx_TP_high;
};

#define HCIOPCODE(_OCF, _OGF)     ((_OGF)<<10|(_OCF))
#define HCIOPCODELOW(_OCF, _OGF)	(u8)(HCIOPCODE(_OCF, _OGF)&0x00ff)
#define HCIOPCODEHIGHT(_OCF, _OGF) (u8)(HCIOPCODE(_OCF, _OGF)>>8)
#define HCI_OGF(opCode)  (unsigned char)((0xFC00 & (opCode)) >> 10)
#define HCI_OCF(opCode)  ( 0x3FF & (opCode))


typedef enum _HCI_STATUS{
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
}RTW_HCI_STATUS;

#define HCI_EVENT_COMMAND_COMPLETE					0x0e

#define OGF_EXTENSION									0X3f
typedef enum HCI_EXTENSION_COMMANDS{
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
	HCI_QUERY_RF_STATUS								=0x0104,
	HCI_BT_ABNORMAL_NOTIFY							=0x0105,
	HCI_BT_INFO_NOTIFY								=0x0106,
	HCI_BT_COEX_NOTIFY								=0x0107,
	HCI_BT_PATCH_VERSION_NOTIFY						=0x0108,
	HCI_BT_AFH_MAP_NOTIFY							=0x0109,
	HCI_BT_REGISTER_VALUE_NOTIFY					=0x010a,
	
	//The following is for IVT
	HCI_WIFI_CURRENT_CHANNEL						=0x0300,	
	HCI_WIFI_CURRENT_BANDWIDTH						=0x0301,		
	HCI_WIFI_CONNECTION_STATUS						=0x0302
}RTW_HCI_EXT_CMD;

#define HCI_EVENT_EXTENSION_RTK						0xfe
typedef enum HCI_EXTENSION_EVENT_RTK{
	HCI_EVENT_EXT_WIFI_SCAN_NOTIFY								=0x01,
	HCI_EVENT_EXT_WIFI_RF_STATUS_NOTIFY						=0x02,
	HCI_EVENT_EXT_BT_INFO_CONTROL								=0x03,
	HCI_EVENT_EXT_BT_COEX_CONTROL								=0x04
}RTW_HCI_EXT_EVENT;

typedef enum _BT_TRAFFIC_MODE{
	BT_MOTOR_EXT_BE		= 0x00, //Best Effort. Default. for HCRP, PAN, SDP, RFCOMM-based profiles like FTP,OPP, SPP, DUN, etc.
	BT_MOTOR_EXT_GUL		= 0x01, //Guaranteed Latency. This type of traffic is used e.g. for HID and AVRCP.
	BT_MOTOR_EXT_GUB		= 0X02, //Guaranteed Bandwidth.
	BT_MOTOR_EXT_GULB	= 0X03  //Guaranteed Latency and Bandwidth. for A2DP and VDP.
} BT_TRAFFIC_MODE;

typedef enum _BT_TRAFFIC_MODE_PROFILE{
	BT_PROFILE_NONE,	
	BT_PROFILE_A2DP,
	BT_PROFILE_PAN	,
	BT_PROFILE_HID,
	BT_PROFILE_SCO		
} BT_TRAFFIC_MODE_PROFILE;

typedef enum _HCI_EXT_BT_OPERATION {
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

typedef struct _BT_MGNT{
	BOOLEAN				bBTConnectInProgress;
	BOOLEAN				bLogLinkInProgress;
	BOOLEAN				bPhyLinkInProgress;
	BOOLEAN				bPhyLinkInProgressStartLL;
	u1Byte				BtCurrentPhyLinkhandle;
	u2Byte				BtCurrentLogLinkhandle;	
	u1Byte				CurrentConnectEntryNum;
	u1Byte				DisconnectEntryNum;
	u1Byte				CurrentBTConnectionCnt;
	BT_CONNECT_TYPE		BTCurrentConnectType;
	BT_CONNECT_TYPE		BTReceiveConnectPkt;	
	u1Byte				BTAuthCount;
	u1Byte				BTAsocCount;
	BOOLEAN				bStartSendSupervisionPkt;
	BOOLEAN				BtOperationOn;
	BOOLEAN				BTNeedAMPStatusChg;
	BOOLEAN				JoinerNeedSendAuth;
	HCI_PHY_LINK_BSS_INFO	bssDesc;
	HCI_EXT_CONFIG		ExtConfig;
	BOOLEAN				bNeedNotifyAMPNoCap;
	BOOLEAN				bCreateSpportQos;
	BOOLEAN				bSupportProfile;
	u1Byte				BTChannel;
	BOOLEAN				CheckChnlIsSuit;
	BOOLEAN				bBtScan;
	BOOLEAN				btLogoTest;
	BOOLEAN				bRfStatusNotified;
	BOOLEAN				bBtRsvedPageDownload;
}BT_MGNT, *PBT_MGNT;

struct bt_coex_info {
	/* For Kernel Socket */
	struct socket *udpsock; 
	struct sockaddr_in wifi_sockaddr; /*wifi socket*/
	struct sockaddr_in bt_sockaddr;/* BT socket  */
	struct sock *sk_store;/*back up socket for UDP RX int*/
	
	/* store which socket is OK */
	u8 sock_open;
		
	u8 BT_attend;
	u8 is_exist; /* socket exist */
	BT_MGNT BtMgnt;
	struct workqueue_struct *btcoex_wq;
	struct delayed_work recvmsg_work;
};
#endif //CONFIG_BT_COEXIST_SOCKET_TRX

#define	PACKET_NORMAL			0
#define	PACKET_DHCP				1
#define	PACKET_ARP				2
#define	PACKET_EAPOL			3

void rtw_btcoex_Initialize(PADAPTER);
void rtw_btcoex_PowerOnSetting(PADAPTER padapter);
void rtw_btcoex_PreLoadFirmware(PADAPTER padapter);
void rtw_btcoex_HAL_Initialize(PADAPTER padapter, u8 bWifiOnly);
void rtw_btcoex_IpsNotify(PADAPTER, u8 type);
void rtw_btcoex_LpsNotify(PADAPTER, u8 type);
void rtw_btcoex_ScanNotify(PADAPTER, u8 type);
void rtw_btcoex_ConnectNotify(PADAPTER, u8 action);
void rtw_btcoex_MediaStatusNotify(PADAPTER, u8 mediaStatus);
void rtw_btcoex_SpecialPacketNotify(PADAPTER, u8 pktType);
void rtw_btcoex_IQKNotify(PADAPTER padapter, u8 state);
void rtw_btcoex_BtInfoNotify(PADAPTER, u8 length, u8 *tmpBuf);
void rtw_btcoex_BtMpRptNotify(PADAPTER, u8 length, u8 *tmpBuf);
void rtw_btcoex_SuspendNotify(PADAPTER, u8 state);
void rtw_btcoex_HaltNotify(PADAPTER);
void rtw_btcoex_ScoreBoardStatusNotify(PADAPTER, u8 length, u8 *tmpBuf);
void rtw_btcoex_SwitchBtTRxMask(PADAPTER);
void rtw_btcoex_Switch(PADAPTER, u8 enable);
u8 rtw_btcoex_IsBtDisabled(PADAPTER);
void rtw_btcoex_Handler(PADAPTER);
s32 rtw_btcoex_IsBTCoexRejectAMPDU(PADAPTER padapter);
s32 rtw_btcoex_IsBTCoexCtrlAMPDUSize(PADAPTER);
u32 rtw_btcoex_GetAMPDUSize(PADAPTER);
void rtw_btcoex_SetManualControl(PADAPTER, u8 bmanual);
u8 rtw_btcoex_1Ant(PADAPTER);
u8 rtw_btcoex_IsBtControlLps(PADAPTER);
u8 rtw_btcoex_IsLpsOn(PADAPTER);
u8 rtw_btcoex_RpwmVal(PADAPTER);
u8 rtw_btcoex_LpsVal(PADAPTER);
void rtw_btcoex_SetBTCoexist(PADAPTER, u8 bBtExist);
void rtw_btcoex_SetChipType(PADAPTER, u8 chipType);
void rtw_btcoex_SetPGAntNum(PADAPTER, u8 antNum);
u8 rtw_btcoex_GetPGAntNum(PADAPTER);
void rtw_btcoex_SetSingleAntPath(PADAPTER padapter, u8 singleAntPath);
u32 rtw_btcoex_GetRaMask(PADAPTER);
void rtw_btcoex_RecordPwrMode(PADAPTER, u8 *pCmdBuf, u8 cmdLen);
void rtw_btcoex_DisplayBtCoexInfo(PADAPTER, u8 *pbuf, u32 bufsize);
void rtw_btcoex_SetDBG(PADAPTER, u32 *pDbgModule);
u32 rtw_btcoex_GetDBG(PADAPTER, u8 *pStrBuf, u32 bufSize);
u8 rtw_btcoex_IncreaseScanDeviceNum(PADAPTER);
u8 rtw_btcoex_IsBtLinkExist(PADAPTER);
void rtw_btcoex_BTOffOnNotify(PADAPTER padapter, u8 bBTON);
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
void rtw_btcoex_SetBtPatchVersion(PADAPTER padapter,u16 btHciVer, u16 btPatchVer);
void rtw_btcoex_SetHciVersion(PADAPTER  padapter, u16 hciVersion);
void rtw_btcoex_StackUpdateProfileInfo(void);
void rtw_btcoex_init_socket(_adapter *padapter);
void rtw_btcoex_close_socket(_adapter *padapter);
void rtw_btcoex_dump_tx_msg(u8 *tx_msg, u8 len, u8 *msg_name);
u8 rtw_btcoex_sendmsgbysocket(_adapter *padapter, u8 *msg, u8 msg_size, bool force);
u8 rtw_btcoex_create_kernel_socket(_adapter *padapter);
void rtw_btcoex_close_kernel_socket(_adapter *padapter);
void rtw_btcoex_recvmsgbysocket(void *data);
u16 rtw_btcoex_parse_recv_data(u8 *msg, u8 msg_size);
u8 rtw_btcoex_btinfo_cmd(PADAPTER padapter, u8 *pbuf, u16 length);
void rtw_btcoex_parse_hci_cmd(_adapter *padapter, u8 *cmd, u16 len);
void rtw_btcoex_SendEventExtBtCoexControl(PADAPTER Adapter, u8 bNeedDbgRsp, u8 dataLen, void *pData);
void rtw_btcoex_SendEventExtBtInfoControl(PADAPTER Adapter, u8 dataLen, void *pData);
void rtw_btcoex_SendScanNotify(PADAPTER padapter, u8 scanType);
#define BT_SendEventExtBtCoexControl(Adapter, bNeedDbgRsp, dataLen, pData) rtw_btcoex_SendEventExtBtCoexControl(Adapter, bNeedDbgRsp, dataLen, pData)
#define BT_SendEventExtBtInfoControl(Adapter, dataLen, pData) rtw_btcoex_SendEventExtBtInfoControl(Adapter, dataLen, pData)
#endif //CONFIG_BT_COEXIST_SOCKET_TRX
u16 rtw_btcoex_btreg_read(PADAPTER padapter, u8 type, u16 addr, u32 *data);
u16 rtw_btcoex_btreg_write(PADAPTER padapter, u8 type, u16 addr, u16 val);

// ==================================================
// Below Functions are called by BT-Coex
// ==================================================
void rtw_btcoex_rx_ampdu_apply(PADAPTER);
void rtw_btcoex_LPS_Enter(PADAPTER);
void rtw_btcoex_LPS_Leave(PADAPTER);

#endif // __RTW_BTCOEX_H__

