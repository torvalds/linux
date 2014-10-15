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

#ifndef __RTW_BT_MP_H
#define __RTW_BT_MP_H


#if(MP_DRIVER == 1)

#pragma pack(1)

// definition for BT_UP_OP_BT_READY
#define	MP_BT_NOT_READY						0
#define	MP_BT_READY							1

// definition for BT_UP_OP_BT_SET_MODE
typedef enum _MP_BT_MODE{
	MP_BT_MODE_RF_TXRX_TEST_MODE							= 0,
	MP_BT_MODE_BT20_DUT_TEST_MODE							= 1,
	MP_BT_MODE_BT40_DIRECT_TEST_MODE						= 2,
	MP_BT_MODE_CONNECT_TEST_MODE							= 3,
	MP_BT_MODE_MAX
}MP_BT_MODE,*PMP_BT_MODE;


// definition for BT_UP_OP_BT_SET_TX_RX_PARAMETER
typedef struct _BT_TXRX_PARAMETERS{
    u1Byte		txrxChannel;
    u4Byte		txrxTxPktCnt;
    u1Byte		txrxTxPktInterval;
	u1Byte		txrxPayloadType;
	u1Byte		txrxPktType;
	u2Byte		txrxPayloadLen;
	u4Byte		txrxPktHeader;
	u1Byte		txrxWhitenCoeff;
	u1Byte		txrxBdaddr[6];
	u1Byte		txrxTxGainIndex;
} BT_TXRX_PARAMETERS, *PBT_TXRX_PARAMETERS;

// txrxPktType
typedef enum _MP_BT_PKT_TYPE{
	MP_BT_PKT_DH1							= 0,
	MP_BT_PKT_DH3							= 1,
	MP_BT_PKT_DH5							= 2,
	MP_BT_PKT_2DH1							= 3,
	MP_BT_PKT_2DH3							= 4,
	MP_BT_PKT_2DH5							= 5,
	MP_BT_PKT_3DH1							= 6,
	MP_BT_PKT_3DH3							= 7,
	MP_BT_PKT_3DH5							= 8,
	MP_BT_PKT_LE							= 9,
	MP_BT_PKT_MAX
}MP_BT_PKT_TYPE,*PMP_BT_PKT_TYPE;
// txrxPayloadType
typedef enum _MP_BT_PAYLOAD_TYPE{
	MP_BT_PAYLOAD_01010101					= 0,
	MP_BT_PAYLOAD_ALL_1						= 1,
	MP_BT_PAYLOAD_ALL_0						= 2,
	MP_BT_PAYLOAD_11110000					= 3,
	MP_BT_PAYLOAD_PRBS9						= 4,
	MP_BT_PAYLOAD_MAX
}MP_BT_PAYLOAD_TYPE,*PMP_BT_PAYLOAD_TYPE;


// definition for BT_UP_OP_BT_TEST_CTRL
typedef enum _MP_BT_TEST_CTRL{
	MP_BT_TEST_STOP_ALL_TESTS						= 0,
	MP_BT_TEST_START_RX_TEST						= 1,
	MP_BT_TEST_START_PACKET_TX_TEST					= 2,
	MP_BT_TEST_START_CONTINUOUS_TX_TEST 			= 3,
	MP_BT_TEST_START_INQUIRY_SCAN_TEST				= 4,
	MP_BT_TEST_START_PAGE_SCAN_TEST					= 5,
	MP_BT_TEST_START_INQUIRY_PAGE_SCAN_TEST			= 6,
	MP_BT_TEST_START_LEGACY_CONNECT_TEST			= 7,
	MP_BT_TEST_START_LE_CONNECT_TEST_INITIATOR 		= 8,
	MP_BT_TEST_START_LE_CONNECT_TEST_ADVERTISER 	= 9,
	MP_BT_TEST_MAX
}MP_BT_TEST_CTRL,*PMP_BT_TEST_CTRL;


typedef enum _RTL_EXT_C2H_EVT
{
	EXT_C2H_WIFI_FW_ACTIVE_RSP = 0,
	EXT_C2H_TRIG_BY_BT_FW = 1,
	MAX_EXT_C2HEVENT
}RTL_EXT_C2H_EVT;


// return status definition to the user layer
typedef enum _BT_CTRL_STATUS{
	BT_STATUS_SUCCESS									= 0x00, // Success
	BT_STATUS_BT_OP_SUCCESS 							= 0x01, // bt fw op execution success
	BT_STATUS_H2C_SUCCESS								= 0x02, // H2c success
	BT_STATUS_H2C_TIMTOUT								= 0x03, // H2c timeout
	BT_STATUS_H2C_BT_NO_RSP 							= 0x04, // H2c sent, bt no rsp
	BT_STATUS_C2H_SUCCESS								= 0x05, // C2h success
	BT_STATUS_C2H_REQNUM_MISMATCH						= 0x06, // bt fw wrong rsp
	BT_STATUS_OPCODE_U_VERSION_MISMATCH 				= 0x07, // Upper layer OP code version mismatch.
	BT_STATUS_OPCODE_L_VERSION_MISMATCH 				= 0x08, // Lower layer OP code version mismatch.
	BT_STATUS_UNKNOWN_OPCODE_U							= 0x09, // Unknown Upper layer OP code
	BT_STATUS_UNKNOWN_OPCODE_L							= 0x0a, // Unknown Lower layer OP code
	BT_STATUS_PARAMETER_FORMAT_ERROR_U					= 0x0b, // Wrong parameters sent by upper layer.
	BT_STATUS_PARAMETER_FORMAT_ERROR_L					= 0x0c, // bt fw parameter format is not consistency
	BT_STATUS_PARAMETER_OUT_OF_RANGE_U					= 0x0d, // uppery layer parameter value is out of range
	BT_STATUS_PARAMETER_OUT_OF_RANGE_L					= 0x0e, // bt fw parameter value is out of range
	BT_STATUS_UNKNOWN_STATUS_L							= 0x0f, // bt returned an defined status code
	BT_STATUS_UNKNOWN_STATUS_H							= 0x10, // driver need to do error handle or not handle-well.
	BT_STATUS_WRONG_LEVEL								= 0x11, // should be under passive level
	BT_STATUS_MAX
}BT_CTRL_STATUS,*PBT_CTRL_STATUS;

// OP codes definition between the user layer and driver
typedef enum _BT_CTRL_OPCODE_UPPER{
	BT_UP_OP_BT_READY										= 0x00, 
	BT_UP_OP_BT_SET_MODE									= 0x01,
	BT_UP_OP_BT_SET_TX_RX_PARAMETER 						= 0x02,
	BT_UP_OP_BT_SET_GENERAL 								= 0x03,
	BT_UP_OP_BT_GET_GENERAL 								= 0x04,
	BT_UP_OP_BT_TEST_CTRL									= 0x05,
	BT_UP_OP_TEST_BT										= 0x06,
	BT_UP_OP_MAX
}BT_CTRL_OPCODE_UPPER,*PBT_CTRL_OPCODE_UPPER;


typedef enum _BT_SET_GENERAL{
	BT_GSET_REG 											= 0x00, 
	BT_GSET_RESET											= 0x01, 
	BT_GSET_TARGET_BD_ADDR									= 0x02, 
	BT_GSET_TX_PWR_FINETUNE 								= 0x03,
	BT_SET_TRACKING_INTERVAL								= 0x04,
	BT_SET_THERMAL_METER									= 0x05,
	BT_ENABLE_CFO_TRACKING									= 0x06,									
	BT_GSET_UPDATE_BT_PATCH 								= 0x07,
	BT_GSET_MAX
}BT_SET_GENERAL,*PBT_SET_GENERAL;

typedef enum _BT_GET_GENERAL{
	BT_GGET_REG 											= 0x00, 
	BT_GGET_STATUS											= 0x01,
	BT_GGET_REPORT											= 0x02,
	BT_GGET_AFH_MAP 										= 0x03,
	BT_GGET_AFH_STATUS										= 0x04,
	BT_GGET_MAX
}BT_GET_GENERAL,*PBT_GET_GENERAL;

// definition for BT_UP_OP_BT_SET_GENERAL
typedef enum _BT_REG_TYPE{
	BT_REG_RF								= 0,
	BT_REG_MODEM							= 1,
	BT_REG_BLUEWIZE 						= 2,
	BT_REG_VENDOR							= 3,
	BT_REG_LE								= 4,
	BT_REG_MAX
}BT_REG_TYPE,*PBT_REG_TYPE;

// definition for BT_LO_OP_GET_AFH_MAP
typedef enum _BT_AFH_MAP_TYPE{
	BT_AFH_MAP_RESULT						= 0,
	BT_AFH_MAP_WIFI_PSD_ONLY				= 1,
	BT_AFH_MAP_WIFI_CH_BW_ONLY				= 2,
	BT_AFH_MAP_BT_PSD_ONLY					= 3,
	BT_AFH_MAP_HOST_CLASSIFICATION_ONLY 	= 4,
	BT_AFH_MAP_MAX
}BT_AFH_MAP_TYPE,*PBT_AFH_MAP_TYPE;

// definition for BT_UP_OP_BT_GET_GENERAL
typedef enum _BT_REPORT_TYPE{
	BT_REPORT_RX_PACKET_CNT 				= 0,
	BT_REPORT_RX_ERROR_BITS 				= 1,
	BT_REPORT_RSSI							= 2,
	BT_REPORT_CFO_HDR_QUALITY				= 3,
	BT_REPORT_CONNECT_TARGET_BD_ADDR		= 4,
	BT_REPORT_MAX
}BT_REPORT_TYPE,*PBT_REPORT_TYPE;

VOID
MPTBT_Test(
	IN	PADAPTER	Adapter,
	IN	u1Byte		opCode,
	IN	u1Byte		byte1,
	IN	u1Byte		byte2,
	IN	u1Byte		byte3
	);

NDIS_STATUS
MPTBT_SendOidBT(
	IN	PADAPTER		pAdapter,
	IN	PVOID			InformationBuffer,
	IN	ULONG			InformationBufferLength,
	OUT	PULONG			BytesRead,
	OUT	PULONG			BytesNeeded
	);

VOID
MPTBT_FwC2hBtMpCtrl(
	PADAPTER	Adapter,
	pu1Byte 	tmpBuf,
	u1Byte		length
	);

void MPh2c_timeout_handle(void *FunctionContext);

VOID mptbt_BtControlProcess(
	PADAPTER	Adapter,
	PVOID		pInBuf
	);

#define	BT_H2C_MAX_RETRY								1
#define	BT_MAX_C2H_LEN								20

typedef struct _BT_REQ_CMD{
    UCHAR       opCodeVer;
    UCHAR       OpCode;
    USHORT      paraLength;
    UCHAR       pParamStart[100];
} BT_REQ_CMD, *PBT_REQ_CMD;

typedef struct _BT_RSP_CMD{
    USHORT      status;
    USHORT      paraLength;
    UCHAR       pParamStart[100];
} BT_RSP_CMD, *PBT_RSP_CMD;


typedef struct _BT_H2C{
	u1Byte	opCodeVer:4;
	u1Byte	reqNum:4;
	u1Byte	opCode;
	u1Byte	buf[100];
}BT_H2C, *PBT_H2C;



typedef struct _BT_EXT_C2H{
	u1Byte	extendId;
	u1Byte	statusCode:4;
	u1Byte	retLen:4;
	u1Byte	opCodeVer:4;
	u1Byte	reqNum:4;
	u1Byte	buf[100];
}BT_EXT_C2H, *PBT_EXT_C2H;


typedef enum _BT_OPCODE_STATUS{
	BT_OP_STATUS_SUCCESS									= 0x00, // Success
	BT_OP_STATUS_VERSION_MISMATCH							= 0x01,	
	BT_OP_STATUS_UNKNOWN_OPCODE								= 0x02,
	BT_OP_STATUS_ERROR_PARAMETER							= 0x03,
	BT_OP_STATUS_MAX
}BT_OPCODE_STATUS,*PBT_OPCODE_STATUS;



//OP codes definition between driver and bt fw
typedef enum _BT_CTRL_OPCODE_LOWER{
	BT_LO_OP_GET_BT_VERSION 									= 0x00, 
	BT_LO_OP_RESET												= 0x01,
	BT_LO_OP_TEST_CTRL											= 0x02,
	BT_LO_OP_SET_BT_MODE										= 0x03,
	BT_LO_OP_SET_CHNL_TX_GAIN									= 0x04,
	BT_LO_OP_SET_PKT_TYPE_LEN									= 0x05,
	BT_LO_OP_SET_PKT_CNT_L_PL_TYPE								= 0x06,
	BT_LO_OP_SET_PKT_CNT_H_PKT_INTV 							= 0x07,
	BT_LO_OP_SET_PKT_HEADER 									= 0x08,
	BT_LO_OP_SET_WHITENCOEFF									= 0x09,
	BT_LO_OP_SET_BD_ADDR_L										= 0x0a,
	BT_LO_OP_SET_BD_ADDR_H										= 0x0b,
	BT_LO_OP_WRITE_REG_ADDR 									= 0x0c,
	BT_LO_OP_WRITE_REG_VALUE									= 0x0d,
	BT_LO_OP_GET_BT_STATUS										= 0x0e,
	BT_LO_OP_GET_BD_ADDR_L										= 0x0f,
	BT_LO_OP_GET_BD_ADDR_H										= 0x10,
	BT_LO_OP_READ_REG											= 0x11,
	BT_LO_OP_SET_TARGET_BD_ADDR_L								= 0x12,
	BT_LO_OP_SET_TARGET_BD_ADDR_H								= 0x13,
	BT_LO_OP_SET_TX_POWER_CALIBRATION							= 0x14,
	BT_LO_OP_GET_RX_PKT_CNT_L									= 0x15,
	BT_LO_OP_GET_RX_PKT_CNT_H									= 0x16,
	BT_LO_OP_GET_RX_ERROR_BITS_L								= 0x17,
	BT_LO_OP_GET_RX_ERROR_BITS_H								= 0x18,
	BT_LO_OP_GET_RSSI											= 0x19,
	BT_LO_OP_GET_CFO_HDR_QUALITY_L								= 0x1a,
	BT_LO_OP_GET_CFO_HDR_QUALITY_H								= 0x1b,
	BT_LO_OP_GET_TARGET_BD_ADDR_L								= 0x1c,
	BT_LO_OP_GET_TARGET_BD_ADDR_H								= 0x1d,
	BT_LO_OP_GET_AFH_MAP_L										= 0x1e,
	BT_LO_OP_GET_AFH_MAP_M										= 0x1f,
	BT_LO_OP_GET_AFH_MAP_H										= 0x20,
	BT_LO_OP_GET_AFH_STATUS 									= 0x21,
	BT_LO_OP_SET_TRACKING_INTERVAL								= 0x22,
	BT_LO_OP_SET_THERMAL_METER									= 0x23,
	BT_LO_OP_ENABLE_CFO_TRACKING								= 0x24,
	BT_LO_OP_MAX
}BT_CTRL_OPCODE_LOWER,*PBT_CTRL_OPCODE_LOWER;




#endif  /* #if(MP_DRIVER == 1) */

#endif // #ifndef __INC_MPT_BT_H

