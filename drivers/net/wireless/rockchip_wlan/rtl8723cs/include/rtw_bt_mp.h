/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/

#ifndef __RTW_BT_MP_H
#define __RTW_BT_MP_H


#if (MP_DRIVER == 1)

#pragma pack(1)

/* definition for BT_UP_OP_BT_READY */
#define	MP_BT_NOT_READY						0
#define	MP_BT_READY							1

/* definition for BT_UP_OP_BT_SET_MODE */
typedef enum _MP_BT_MODE {
	MP_BT_MODE_RF_TXRX_TEST_MODE							= 0,
	MP_BT_MODE_BT20_DUT_TEST_MODE							= 1,
	MP_BT_MODE_BT40_DIRECT_TEST_MODE						= 2,
	MP_BT_MODE_CONNECT_TEST_MODE							= 3,
	MP_BT_MODE_MAX
} MP_BT_MODE, *PMP_BT_MODE;


/* definition for BT_UP_OP_BT_SET_TX_RX_PARAMETER */
typedef struct _BT_TXRX_PARAMETERS {
	u8		txrxChannel;
	u32		txrxTxPktCnt;
	u8		txrxTxPktInterval;
	u8		txrxPayloadType;
	u8		txrxPktType;
	u16		txrxPayloadLen;
	u32		txrxPktHeader;
	u8		txrxWhitenCoeff;
	u8		txrxBdaddr[6];
	u8		txrxTxGainIndex;
} BT_TXRX_PARAMETERS, *PBT_TXRX_PARAMETERS;

/* txrxPktType */
typedef enum _MP_BT_PKT_TYPE {
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
} MP_BT_PKT_TYPE, *PMP_BT_PKT_TYPE;
/* txrxPayloadType */
typedef enum _MP_BT_PAYLOAD_TYPE {
	MP_BT_PAYLOAD_01010101					= 0,
	MP_BT_PAYLOAD_ALL_1						= 1,
	MP_BT_PAYLOAD_ALL_0						= 2,
	MP_BT_PAYLOAD_11110000					= 3,
	MP_BT_PAYLOAD_PRBS9						= 4,
	MP_BT_PAYLOAD_MAX						= 8,
} MP_BT_PAYLOAD_TYPE, *PMP_BT_PAYLOAD_TYPE;


/* definition for BT_UP_OP_BT_TEST_CTRL */
typedef enum _MP_BT_TEST_CTRL {
	MP_BT_TEST_STOP_ALL_TESTS						= 0,
	MP_BT_TEST_START_RX_TEST						= 1,
	MP_BT_TEST_START_PACKET_TX_TEST					= 2,
	MP_BT_TEST_START_CONTINUOUS_TX_TEST			= 3,
	MP_BT_TEST_START_INQUIRY_SCAN_TEST				= 4,
	MP_BT_TEST_START_PAGE_SCAN_TEST					= 5,
	MP_BT_TEST_START_INQUIRY_PAGE_SCAN_TEST			= 6,
	MP_BT_TEST_START_LEGACY_CONNECT_TEST			= 7,
	MP_BT_TEST_START_LE_CONNECT_TEST_INITIATOR		= 8,
	MP_BT_TEST_START_LE_CONNECT_TEST_ADVERTISER	= 9,
	MP_BT_TEST_MAX
} MP_BT_TEST_CTRL, *PMP_BT_TEST_CTRL;


typedef enum _RTL_EXT_C2H_EVT {
	EXT_C2H_WIFI_FW_ACTIVE_RSP = 0,
	EXT_C2H_TRIG_BY_BT_FW = 1,
	MAX_EXT_C2HEVENT
} RTL_EXT_C2H_EVT;

/* OP codes definition between the user layer and driver */
typedef enum _BT_CTRL_OPCODE_UPPER {
	BT_UP_OP_BT_READY										= 0x00,
	BT_UP_OP_BT_SET_MODE									= 0x01,
	BT_UP_OP_BT_SET_TX_RX_PARAMETER						= 0x02,
	BT_UP_OP_BT_SET_GENERAL								= 0x03,
	BT_UP_OP_BT_GET_GENERAL								= 0x04,
	BT_UP_OP_BT_TEST_CTRL									= 0x05,
	BT_UP_OP_TEST_BT										= 0x06,
	BT_UP_OP_MAX
} BT_CTRL_OPCODE_UPPER, *PBT_CTRL_OPCODE_UPPER;


typedef enum _BT_SET_GENERAL {
	BT_GSET_REG											= 0x00,
	BT_GSET_RESET											= 0x01,
	BT_GSET_TARGET_BD_ADDR									= 0x02,
	BT_GSET_TX_PWR_FINETUNE								= 0x03,
	BT_SET_TRACKING_INTERVAL								= 0x04,
	BT_SET_THERMAL_METER									= 0x05,
	BT_ENABLE_CFO_TRACKING									= 0x06,
	BT_GSET_UPDATE_BT_PATCH								= 0x07,
	BT_GSET_MAX
} BT_SET_GENERAL, *PBT_SET_GENERAL;

typedef enum _BT_GET_GENERAL {
	BT_GGET_REG											= 0x00,
	BT_GGET_STATUS											= 0x01,
	BT_GGET_REPORT											= 0x02,
	BT_GGET_AFH_MAP										= 0x03,
	BT_GGET_AFH_STATUS										= 0x04,
	BT_GGET_MAX
} BT_GET_GENERAL, *PBT_GET_GENERAL;

/* definition for BT_UP_OP_BT_SET_GENERAL */
typedef enum _BT_REG_TYPE {
	BT_REG_RF								= 0,
	BT_REG_MODEM							= 1,
	BT_REG_BLUEWIZE						= 2,
	BT_REG_VENDOR							= 3,
	BT_REG_LE								= 4,
	BT_REG_MAX
} BT_REG_TYPE, *PBT_REG_TYPE;

/* definition for BT_LO_OP_GET_AFH_MAP */
typedef enum _BT_AFH_MAP_TYPE {
	BT_AFH_MAP_RESULT						= 0,
	BT_AFH_MAP_WIFI_PSD_ONLY				= 1,
	BT_AFH_MAP_WIFI_CH_BW_ONLY				= 2,
	BT_AFH_MAP_BT_PSD_ONLY					= 3,
	BT_AFH_MAP_HOST_CLASSIFICATION_ONLY	= 4,
	BT_AFH_MAP_MAX
} BT_AFH_MAP_TYPE, *PBT_AFH_MAP_TYPE;

/* definition for BT_UP_OP_BT_GET_GENERAL */
typedef enum _BT_REPORT_TYPE {
	BT_REPORT_RX_PACKET_CNT				= 0,
	BT_REPORT_RX_ERROR_BITS				= 1,
	BT_REPORT_RSSI							= 2,
	BT_REPORT_CFO_HDR_QUALITY				= 3,
	BT_REPORT_CONNECT_TARGET_BD_ADDR		= 4,
	BT_REPORT_MAX
} BT_REPORT_TYPE, *PBT_REPORT_TYPE;

void
MPTBT_Test(
		PADAPTER	Adapter,
		u8		opCode,
		u8		byte1,
		u8		byte2,
		u8		byte3
);

uint
MPTBT_SendOidBT(
		PADAPTER		pAdapter,
		void				*InformationBuffer,
		u32				InformationBufferLength,
		u32 				*BytesRead,
		u32 				*BytesNeeded
);

void
MPTBT_FwC2hBtMpCtrl(
	PADAPTER	Adapter,
	u8 			*tmpBuf,
	u8			length
);

void MPh2c_timeout_handle(void *FunctionContext);

void mptbt_BtControlProcess(
	PADAPTER	Adapter,
	void			*pInBuf
);

#define	BT_H2C_MAX_RETRY								1
#define	BT_MAX_C2H_LEN								20

typedef struct _BT_REQ_CMD {
	u8       opCodeVer;
	u8       OpCode;
	u16      paraLength;
	u8       pParamStart[100];
} BT_REQ_CMD, *PBT_REQ_CMD;

typedef struct _BT_RSP_CMD {
	u16      status;
	u16      paraLength;
	u8       pParamStart[100];
} BT_RSP_CMD, *PBT_RSP_CMD;


typedef struct _BT_H2C {
	u8	opCodeVer:4;
	u8	reqNum:4;
	u8	opCode;
	u8	buf[100];
} BT_H2C, *PBT_H2C;



typedef struct _BT_EXT_C2H {
	u8	extendId;
	u8	statusCode:4;
	u8	retLen:4;
	u8	opCodeVer:4;
	u8	reqNum:4;
	u8	buf[100];
} BT_EXT_C2H, *PBT_EXT_C2H;


typedef enum _BT_OPCODE_STATUS {
	BT_OP_STATUS_SUCCESS									= 0x00, /* Success */
	BT_OP_STATUS_VERSION_MISMATCH							= 0x01,
	BT_OP_STATUS_UNKNOWN_OPCODE								= 0x02,
	BT_OP_STATUS_ERROR_PARAMETER							= 0x03,
	BT_OP_STATUS_MAX
} BT_OPCODE_STATUS, *PBT_OPCODE_STATUS;



/* OP codes definition between driver and bt fw */
typedef enum _BT_CTRL_OPCODE_LOWER {
	BT_LO_OP_GET_BT_VERSION									= 0x00,
	BT_LO_OP_RESET												= 0x01,
	BT_LO_OP_TEST_CTRL											= 0x02,
	BT_LO_OP_SET_BT_MODE										= 0x03,
	BT_LO_OP_SET_CHNL_TX_GAIN									= 0x04,
	BT_LO_OP_SET_PKT_TYPE_LEN									= 0x05,
	BT_LO_OP_SET_PKT_CNT_L_PL_TYPE								= 0x06,
	BT_LO_OP_SET_PKT_CNT_H_PKT_INTV							= 0x07,
	BT_LO_OP_SET_PKT_HEADER									= 0x08,
	BT_LO_OP_SET_WHITENCOEFF									= 0x09,
	BT_LO_OP_SET_BD_ADDR_L										= 0x0a,
	BT_LO_OP_SET_BD_ADDR_H										= 0x0b,
	BT_LO_OP_WRITE_REG_ADDR									= 0x0c,
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
	BT_LO_OP_GET_AFH_STATUS									= 0x21,
	BT_LO_OP_SET_TRACKING_INTERVAL								= 0x22,
	BT_LO_OP_SET_THERMAL_METER									= 0x23,
	BT_LO_OP_ENABLE_CFO_TRACKING								= 0x24,
	BT_LO_OP_MAX
} BT_CTRL_OPCODE_LOWER, *PBT_CTRL_OPCODE_LOWER;




#endif  /* #if(MP_DRIVER == 1) */

#endif /*  #ifndef __INC_MPT_BT_H */
