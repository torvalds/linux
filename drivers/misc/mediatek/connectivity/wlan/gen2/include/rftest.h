/*
** Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/rftest.h#1
*/

/*! \file   "rftest.h"
    \brief  definitions for RF Productino test

*/

/*
** Log: rftest.h
 *
 * 12 20 2011 cp.wu
 * [WCXRP00001144] [MT6620 Wi-Fi][Driver][Firmware] Add RF_FUNC_ID for exposing device and related version information
 * add driver implementations for RF_AT_FUNCID_FW_INFO & RF_AT_FUNCID_DRV_INFO
 * to expose version information
 *
 * 08 04 2010 cp.wu
 * NULL
 * add an extra parameter to rftestQueryATInfo 'cause it's necessary to pass u4FuncData for query request.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 04 14 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * information buffer for query oid/ioctl is now buffered in prCmdInfo
 *  *  *  * instead of glue-layer variable to improve multiple oid/ioctl capability
 *
 * 12 30 2009 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1) According to CMD/EVENT documentation v0.8,
 *  *  *  *  *  *  * OID_CUSTOM_TEST_RX_STATUS & OID_CUSTOM_TEST_TX_STATUS is no longer used,
 *  *  *  *  *  *  * and result is retrieved by get ATInfo instead
 *  *  *  *  *  *  * 2) add 4 counter for recording aggregation statistics
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-12-08 17:35:11 GMT mtk02752
**  * comment out RF test which is not supported on MT6620
**  + API decalre for rftest
**  \main\maintrunk.MT6620WiFiDriver_Prj\1 2009-12-08 11:29:07 GMT mtk02752
**  definitions for RF test mode
**
*/
#ifndef _RFTEST_H
#define _RFTEST_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
/* Table Version */
#define RF_AUTO_TEST_FUNCTION_TABLE_VERSION 0x01000001

/* Power */
#define RF_AT_PARAM_POWER_MASK      BITS(0, 7)
#define RF_AT_PARAM_POWER_MAX       RF_AT_PARAM_POWER_MASK

/* Rate */
#define RF_AT_PARAM_RATE_MCS_MASK   BIT(31)
#define RF_AT_PARAM_RATE_MASK       BITS(0, 7)
#define RF_AT_PARAM_RATE_CCK_MAX    3
#define RF_AT_PARAM_RATE_1M         0
#define RF_AT_PARAM_RATE_2M         1
#define RF_AT_PARAM_RATE_5_5M       2
#define RF_AT_PARAM_RATE_11M        3
#define RF_AT_PARAM_RATE_6M         4
#define RF_AT_PARAM_RATE_9M         5
#define RF_AT_PARAM_RATE_12M        6
#define RF_AT_PARAM_RATE_18M        7
#define RF_AT_PARAM_RATE_24M        8
#define RF_AT_PARAM_RATE_36M        9
#define RF_AT_PARAM_RATE_48M        10
#define RF_AT_PARAM_RATE_54M        11

/* Antenna */
#define RF_AT_PARAM_ANTENNA_ID_MASK BITS(0, 7)
#define RF_AT_PARAM_ANTENNA_ID_MAX  1

/* Packet Length */
#define RF_AT_PARAM_TX_80211HDR_BYTE_MAX     (32)
#define RF_AT_PARAM_TX_80211PAYLOAD_BYTE_MAX (2048)

#define RF_AT_PARAM_TX_PKTLEN_BYTE_DEFAULT  1024
#define RF_AT_PARAM_TX_PKTLEN_BYTE_MAX  \
	((UINT_16)(RF_AT_PARAM_TX_80211HDR_BYTE_MAX + RF_AT_PARAM_TX_80211PAYLOAD_BYTE_MAX))

/* Packet Count */
#define RF_AT_PARAM_TX_PKTCNT_DEFAULT    1000
#define RF_AT_PARAM_TX_PKTCNT_UNLIMITED  0

/* Packet Interval */
#define RF_AT_PARAM_TX_PKT_INTERVAL_US_DEFAULT  50

/* ALC */
#define RF_AT_PARAM_ALC_DISABLE     0
#define RF_AT_PARAM_ALC_ENABLE      1

/* TXOP */
#define RF_AT_PARAM_TXOP_DEFAULT    0
#define RF_AT_PARAM_TXOPQUE_QMASK   BITS(16, 31)
#define RF_AT_PARAM_TXOPQUE_TMASK   BITS(0, 15)
#define RF_AT_PARAM_TXOPQUE_AC0     (0<<16)
#define RF_AT_PARAM_TXOPQUE_AC1     (1<<16)
#define RF_AT_PARAM_TXOPQUE_AC2     (2<<16)
#define RF_AT_PARAM_TXOPQUE_AC3     (3<<16)
#define RF_AT_PARAM_TXOPQUE_AC4     (4<<16)
#define RF_AT_PARAM_TXOPQUE_QOFFSET 16

/* Retry Limit */
#define RF_AT_PARAM_TX_RETRY_DEFAULT    0
#define RF_AT_PARAM_TX_RETRY_MAX        6

/* QoS Queue */
#define RF_AT_PARAM_QOSQUE_AC0      0
#define RF_AT_PARAM_QOSQUE_AC1      1
#define RF_AT_PARAM_QOSQUE_AC2      2
#define RF_AT_PARAM_QOSQUE_AC3      3
#define RF_AT_PARAM_QOSQUE_AC4      4
#define RF_AT_PARAM_QOSQUE_DEFAULT  RF_AT_PARAM_QOSQUE_AC0

/* Bandwidth */
#define RF_AT_PARAM_BANDWIDTH_20MHZ             0
#define RF_AT_PARAM_BANDWIDTH_40MHZ             1
#define RF_AT_PARAM_BANDWIDTH_U20_IN_40MHZ      2
#define RF_AT_PARAM_BANDWIDTH_D20_IN_40MHZ      3
#define RF_AT_PARAM_BANDWIDTH_DEFAULT   RF_AT_PARAM_BANDWIDTH_20MHZ

/* GI (Guard Interval) */
#define RF_AT_PARAM_GI_800NS    0
#define RF_AT_PARAM_GI_400NS    1
#define RF_AT_PARAM_GI_DEFAULT  RF_AT_PARAM_GI_800NS

/* STBC */
#define RF_AT_PARAM_STBC_DISABLE    0
#define RF_AT_PARAM_STBC_ENABLE     1

/* RIFS */
#define RF_AT_PARAM_RIFS_DISABLE    0
#define RF_AT_PARAM_RIFS_ENABLE     1

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
/* Function ID List */
typedef enum _ENUM_RF_AT_FUNCID_T {
	RF_AT_FUNCID_VERSION = 0,
	RF_AT_FUNCID_COMMAND,
	RF_AT_FUNCID_POWER,
	RF_AT_FUNCID_RATE,
	RF_AT_FUNCID_PREAMBLE,
	RF_AT_FUNCID_ANTENNA,
	RF_AT_FUNCID_PKTLEN,
	RF_AT_FUNCID_PKTCNT,
	RF_AT_FUNCID_PKTINTERVAL,
	RF_AT_FUNCID_TEMP_COMPEN,
	RF_AT_FUNCID_TXOPLIMIT,
	RF_AT_FUNCID_ACKPOLICY,
	RF_AT_FUNCID_PKTCONTENT,
	RF_AT_FUNCID_RETRYLIMIT,
	RF_AT_FUNCID_QUEUE,
	RF_AT_FUNCID_BANDWIDTH,
	RF_AT_FUNCID_GI,
	RF_AT_FUNCID_STBC,
	RF_AT_FUNCID_CHNL_FREQ,
	RF_AT_FUNCID_RIFS,
	RF_AT_FUNCID_TRSW_TYPE,
	RF_AT_FUNCID_RF_SX_SHUTDOWN,
	RF_AT_FUNCID_PLL_SHUTDOWN,
	RF_AT_FUNCID_SLOW_CLK_MODE,
	RF_AT_FUNCID_ADC_CLK_MODE,
	RF_AT_FUNCID_MEASURE_MODE,
	RF_AT_FUNCID_VOLT_COMPEN,
	RF_AT_FUNCID_DPD_TX_GAIN,
	RF_AT_FUNCID_DPD_MODE,
	RF_AT_FUNCID_TSSI_MODE,
	RF_AT_FUNCID_TX_GAIN_CODE,
	RF_AT_FUNCID_TX_PWR_MODE,

	/* Query command */
	RF_AT_FUNCID_TXED_COUNT = 32,
	RF_AT_FUNCID_TXOK_COUNT,
	RF_AT_FUNCID_RXOK_COUNT,
	RF_AT_FUNCID_RXERROR_COUNT,
	RF_AT_FUNCID_RESULT_INFO,
	RF_AT_FUNCID_TRX_IQ_RESULT,
	RF_AT_FUNCID_TSSI_RESULT,
	RF_AT_FUNCID_DPD_RESULT,
	RF_AT_FUNCID_RXV_DUMP,
	RF_AT_FUNCID_RX_PHY_STATIS,
	RF_AT_FUNCID_MEASURE_RESULT,
	RF_AT_FUNCID_TEMP_SENSOR,
	RF_AT_FUNCID_VOLT_SENSOR,
	RF_AT_FUNCID_READ_EFUSE,
	RF_AT_FUNCID_RX_RSSI,
	RF_AT_FUNCID_FW_INFO,
	RF_AT_FUNCID_DRV_INFO,

	/* Set command */
	RF_AT_FUNCID_SET_DPD_RESULT = 64,
	RF_AT_FUNCID_SET_CW_MODE,
	RF_AT_FUNCID_SET_JAPAN_CH14_FILTER,
	RF_AT_FUNCID_WRITE_EFUSE,
	RF_AT_FUNCID_SET_MAC_ADDRESS
} ENUM_RF_AT_FUNCID_T;

/* Command */
typedef enum _ENUM_RF_AT_COMMAND_T {
	RF_AT_COMMAND_STOPTEST = 0,
	RF_AT_COMMAND_STARTTX,
	RF_AT_COMMAND_STARTRX,
	RF_AT_COMMAND_RESET,
	RF_AT_COMMAND_OUTPUT_POWER,	/* Payload */
	RF_AT_COMMAND_LO_LEAKAGE,	/* Local freq is renamed to Local leakage */
	RF_AT_COMMAND_CARRIER_SUPPR,	/* OFDM (LTF/STF), CCK (PI,PI/2) */
	RF_AT_COMMAND_TRX_IQ_CAL,
	RF_AT_COMMAND_TSSI_CAL,
	RF_AT_COMMAND_DPD_CAL,
	RF_AT_COMMAND_CW,
	RF_AT_COMMAND_NUM
} ENUM_RF_AT_COMMAND_T;

/* Preamble */
typedef enum _ENUM_RF_AT_PREAMBLE_T {
	RF_AT_PREAMBLE_NORMAL = 0,
	RF_AT_PREAMBLE_CCK_SHORT,
	RF_AT_PREAMBLE_11N_MM,
	RF_AT_PREAMBLE_11N_GF,
	RF_AT_PREAMBLE_NUM
} ENUM_RF_AT_PREAMBLE_T;

/* Ack Policy */
typedef enum _ENUM_RF_AT_ACK_POLICY_T {
	RF_AT_ACK_POLICY_NORMAL = 0,
	RF_AT_ACK_POLICY_NOACK,
	RF_AT_ACK_POLICY_NOEXPLICTACK,
	RF_AT_ACK_POLICY_BLOCKACK,
	RF_AT_ACK_POLICY_NUM
} ENUM_RF_AT_ACK_POLICY_T;

typedef enum _ENUM_RF_AUTOTEST_STATE_T {
	RF_AUTOTEST_STATE_STANDBY = 0,
	RF_AUTOTEST_STATE_TX,
	RF_AUTOTEST_STATE_RX,
	RF_AUTOTEST_STATE_RESET,
	RF_AUTOTEST_STATE_OUTPUT_POWER,
	RF_AUTOTEST_STATE_LOCA_FREQUENCY,
	RF_AUTOTEST_STATE_CARRIER_SUPRRESION,
	RF_AUTOTEST_STATE_NUM
} ENUM_RF_AUTOTEST_STATE_T;

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

WLAN_STATUS rftestSetATInfo(IN P_ADAPTER_T prAdapter, UINT_32 u4FuncIndex, UINT_32 u4FuncData);

WLAN_STATUS
rftestQueryATInfo(IN P_ADAPTER_T prAdapter,
		  UINT_32 u4FuncIndex, UINT_32 u4FuncData, OUT PVOID pvQueryBuffer, IN UINT_32 u4QueryBufferLen);

WLAN_STATUS rftestSetFrequency(IN P_ADAPTER_T prAdapter, IN UINT_32 u4FreqInKHz, IN PUINT_32 pu4SetInfoLen);

#endif /* _RFTEST_H */
