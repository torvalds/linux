/*
** Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/nic/hif_tx.h#1
*/

/*
** Log: hif_tx.h
 *
 * 10 07 2010 cp.wu
 * [WCXRP00000083] [MT5931][Driver][FW] Add necessary logic for MT5931 first connection
 * add firmware download for MT5931.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 14 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * fill extra information for revised HIF_TX_HEADER.
 *
 * 06 10 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) add flag on MSDU_INFO_T for indicating BIP frame and forceBasicRate
 * 2) add  packet type for indicating management frames
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 03 10 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * code clean: removing unused variables and structure definitions
 *
 * 02 09 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * 1. Permanent and current MAC address are now retrieved by CMD/EVENT packets instead of hard-coded address
 *  *  * 2. follow MSDN defined behavior when associates to another AP
 *  *  * 3. for firmware download, packet size could be up to 2048 bytes
 *
 * 01 13 2010 tehuang.liu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * Enabled the Burst_End Indication mechanism
 *
 * 01 13 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * TX: fill ucWlanHeaderLength/ucPktFormtId_Flags according to info provided by prMsduInfo
**  \main\maintrunk.MT6620WiFiDriver_Prj\16 2009-12-10 16:43:40 GMT mtk02752
**  code clean
**  \main\maintrunk.MT6620WiFiDriver_Prj\15 2009-11-24 19:55:11 GMT mtk02752
**  adopt HIF_TX_HEADER_T in new data path
**  \main\maintrunk.MT6620WiFiDriver_Prj\14 2009-11-23 17:54:13 GMT mtk02752
**  CMD_HDR_SIZE = (sizeof(WIFI_CMD_T)) to follow up CM's CMD/EVENT documentation
**
**  \main\maintrunk.MT6620WiFiDriver_Prj\13 2009-11-17 22:41:10 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\12 2009-11-17 17:34:07 GMT mtk02752
**  remove HIF_TX_BUFF_COUNT_TC0 (move to nic_tx.h)
**  \main\maintrunk.MT6620WiFiDriver_Prj\11 2009-11-17 12:14:12 GMT mtk02752
**  add initial value for HIF_TX_BUFF_COUNT_TC5
**  \main\maintrunk.MT6620WiFiDriver_Prj\10 2009-11-13 13:54:18 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\9 2009-11-04 14:11:14 GMT mtk01084
**  modify SW TX data format
**  \main\maintrunk.MT6620WiFiDriver_Prj\8 2009-10-29 19:51:53 GMT mtk01084
**  modify FW/ driver interface
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-05-20 12:22:46 GMT mtk01461
**  Add SeqNum field to CMD Header
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-04-17 19:40:52 GMT mtk01461
**  Update the Log Sign
*/

#ifndef _HIF_TX_H
#define _HIF_TX_H

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
/* Maximum buffer size for individual HIF TCQ Buffer */
#define HIF_TX_BUFF_MAX_SIZE                1552	/* Reserved field was not included */

/* Maximum buffer count for individual HIF TCQ */
#define HIF_TX_BUFF_COUNT_TC0               3
#define HIF_TX_BUFF_COUNT_TC1               3
#define HIF_TX_BUFF_COUNT_TC2               3
#define HIF_TX_BUFF_COUNT_TC3               3
#define HIF_TX_BUFF_COUNT_TC4               2

#define TX_HDR_SIZE                         sizeof(HIF_TX_HEADER_T)

#define CMD_HDR_SIZE                        sizeof(WIFI_CMD_T)

#define CMD_PKT_SIZE_FOR_IMAGE              2048	/* !< 2048 Bytes CMD payload buffer */

/*! NIC_HIF_TX_HEADER_T */
/* DW 0, Byte 0,1 */
#define HIF_TX_HDR_TX_BYTE_COUNT_MASK       BITS(0, 11)
#define HIF_TX_HDR_USER_PRIORITY_OFFSET     12

/* DW 0, Byte 2 */
#define HIF_TX_HDR_ETHER_TYPE_OFFSET_MASK   BITS(0, 7)

/* DW 0, Byte 3 */
#define HIF_TX_HDR_IP_CSUM                  BIT(0)
#define HIF_TX_HDR_TCP_CSUM                 BIT(1)
#define HIF_TX_HDR_RESOURCE_MASK            BITS(2, 5)
#define HIF_TX_HDR_RESOURCE_OFFSET     2
#define HIF_TX_HDR_PACKET_TYPE_MASK         BITS(6, 7)
#define HIF_TX_HDR_PACKET_TYPE_OFFSET       6

/* DW 1, Byte 0 */
#define HIF_TX_HDR_WLAN_HEADER_LEN_MASK     BITS(0, 5)

/* DW 1, Byte 1 */
#define HIF_TX_HDR_FORMAT_ID_MASK               BITS(0, 2)
#define HIF_TX_HDR_NETWORK_TYPE_MASK            BITS(4, 5)
#define HIF_TX_HDR_NETWORK_TYPE_OFFSET          4
#define HIF_TX_HDR_FLAG_1X_FRAME_MASK           BIT(6)
#define HIF_TX_HDR_FLAG_1X_FRAME_OFFSET         6
#define HIF_TX_HDR_FLAG_802_11_FORMAT_MASK      BIT(7)
#define HIF_TX_HDR_FLAG_802_11_FORMAT_OFFSET    7

/* DW2, Byte 3 */
#define HIF_TX_HDR_PS_FORWARDING_TYPE_MASK  BITS(0, 1)
#define HIF_TX_HDR_PS_SESSION_ID_MASK       BITS(2, 4)
#define HIF_TX_HDR_PS_SESSION_ID_OFFSET     2
#define HIF_TX_HDR_BURST_END_MASK           BIT(5)
#define HIF_TX_HDR_BURST_END_OFFSET         5

/* DW3, Byte 1 */
#define HIF_TX_HDR_NEED_ACK                 BIT(0)
#define HIF_TX_HDR_BIP                      BIT(1)
#define HIF_TX_HDR_BASIC_RATE               BIT(2)
#define HIF_TX_HDR_NEED_TX_DONE_STATUS      BIT(3)
#define HIF_TX_HDR_RTS                      BIT(4)

/*******************************************************************************
*                         D A T A   T Y P E S
********************************************************************************
*/
typedef struct _HIF_HW_TX_HEADER_T {
	UINT_16 u2TxByteCount;
	UINT_8 ucEtherTypeOffset;
	UINT_8 ucCSflags;
	UINT_8 aucBuffer[0];
} HIF_HW_TX_HEADER_T, *P_HIF_HW_TX_HEADER_T;

typedef struct _HIF_TX_HEADER_T {
	UINT_16 u2TxByteCount_UserPriority;
	UINT_8 ucEtherTypeOffset;
	UINT_8 ucResource_PktType_CSflags;
	UINT_8 ucWlanHeaderLength;
	UINT_8 ucPktFormtId_Flags;
	UINT_16 u2LLH;		/* for BOW */
	UINT_16 u2SeqNo;	/* for BOW */
	UINT_8 ucStaRecIdx;
	UINT_8 ucForwardingType_SessionID_Reserved;
	UINT_8 ucPacketSeqNo;
	UINT_8 ucAck_BIP_BasicRate;
	UINT_8 aucReserved[2];
} HIF_TX_HEADER_T, *P_HIF_TX_HEADER_T;

typedef enum _ENUM_HIF_TX_PKT_TYPE_T {
	HIF_TX_PKT_TYPE_DATA = 0,
	HIF_TX_PKT_TYPE_CMD,
	HIF_TX_PKT_TYPE_HIF_LOOPBACK,
	HIF_TX_PKT_TYPE_MANAGEMENT,
	HIF_TX_PKT_TYPE_NUM
} ENUM_HIF_TX_PKT_TYPE_T, *P_ENUM_HIF_TX_PKT_TYPE_T;

typedef enum _ENUM_HIF_OOB_CTRL_PKT_TYPE_T {
	HIF_OOB_CTRL_PKT_TYPE_LOOPBACK = 1,
	HIF_OOB_CTRL_PKT_TYP_NUM
} ENUM_HIF_OOB_CTRL_PKT_TYPE_T, *P_ENUM_HIF_OOB_CTRL_PKT_TYPE_T;

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
#define TFCB_FRAME_PAD_TO_DW(u2Length)      ALIGN_4(u2Length)

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
/* Kevin: we don't have to call following function to inspect the data structure.
 * It will check automatically while at compile time.
 */
static inline VOID hif_txDataTypeCheck(VOID);

static inline VOID hif_txDataTypeCheck(VOID)
{
	DATA_STRUCT_INSPECTING_ASSERT(sizeof(HIF_TX_HEADER_T) == 16);

}

#endif /*_HIF_TX_H */
