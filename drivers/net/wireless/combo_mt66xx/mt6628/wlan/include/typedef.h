/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/typedef.h#1 $
*/

/*! \file   typedef.h
    \brief  Declaration of data type and return values of internal protocol stack.

    In this file we declare the data type and return values which will be exported
    to the GLUE Layer.
*/



/*
** $Log: typedef.h $
 *
 * 07 18 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings[WCXRP00000612] [MT6620 Wi-Fi] [FW] CSD update SWRDD algorithm
 * Add CMD/Event for RDD and BWCS.
 *
 * 12 30 2010 cp.wu
 * [WCXRP00000327] [MT6620 Wi-Fi][Driver] Improve HEC WHQA 6972 workaround coverage in driver side
 * host driver not to set FW-own when there is still pending interrupts
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 16 2010 kevin.huang
 * NULL
 * Refine AAA functions
 *
 * 07 19 2010 jeffrey.chang
 * 
 * Linux port modification
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 23 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * integrate .
 *
 * 06 18 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver 
 * Provide cnmMgtPktAlloc() and alloc/free function of msg/buf
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * 1) migrate assoc.c.
 * 2) add ucTxSeqNum for tracking frames which needs TX-DONE awareness
 * 3) add configuration options for CNM_MEM and RSN modules
 * 4) add data path for management frames
 * 5) eliminate rPacketInfo of MSDU_INFO_T
 *
 * 06 09 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add necessary changes to driver data paths.
 *
 * 06 09 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add definitions for module migration.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add aa_fsm.h, ais_fsm.h, bss.h, mib.h and scan.h.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 06 03 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * move timer callback to glue layer.
 *
 * 05 31 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add cfg80211 interface, which is to replace WE, for further extension
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * initial import for Linux port
 *
 * 02 24 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add Ethernet destination address information in packet info for TX
 *
 * 02 23 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * add new API: wlanProcessQueuedPackets()
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-03-23 21:41:37 GMT mtk01461
**  Update PACKET_INFO_INIT for TX Path
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-03-23 00:30:17 GMT mtk01461
**  Add parameter in PACKET_INFO_T for HIF Loopback
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-03-18 20:25:22 GMT mtk01461
**  Fix LINT warning
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-16 09:08:28 GMT mtk01461
**  Update TX PATH API
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:11:54 GMT mtk01426
**  Init for develop
**
*/

#ifndef _TYPEDEF_H
#define _TYPEDEF_H

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

/* ieee80211.h of linux has duplicated definitions */
#if defined(WLAN_STATUS_SUCCESS)
#undef WLAN_STATUS_SUCCESS
#endif

#define WLAN_STATUS_SUCCESS                     ((WLAN_STATUS) 0x00000000L)
#define WLAN_STATUS_PENDING                     ((WLAN_STATUS) 0x00000103L)
#define WLAN_STATUS_NOT_ACCEPTED                ((WLAN_STATUS) 0x00010003L)

#define WLAN_STATUS_MEDIA_CONNECT               ((WLAN_STATUS) 0x4001000BL)
#define WLAN_STATUS_MEDIA_DISCONNECT            ((WLAN_STATUS) 0x4001000CL)
#define WLAN_STATUS_MEDIA_SPECIFIC_INDICATION   ((WLAN_STATUS) 0x40010012L)

#define WLAN_STATUS_SCAN_COMPLETE               ((WLAN_STATUS) 0x60010001L)
#define WLAN_STATUS_MSDU_OK                     ((WLAN_STATUS) 0x60010002L)

/* TODO(Kevin): double check if 0x60010001 & 0x60010002 is proprietary */
#define WLAN_STATUS_ROAM_OUT_FIND_BEST          ((WLAN_STATUS) 0x60010101L)
#define WLAN_STATUS_ROAM_DISCOVERY              ((WLAN_STATUS) 0x60010102L)

#define WLAN_STATUS_FAILURE                     ((WLAN_STATUS) 0xC0000001L)
#define WLAN_STATUS_RESOURCES                   ((WLAN_STATUS) 0xC000009AL)
#define WLAN_STATUS_NOT_SUPPORTED               ((WLAN_STATUS) 0xC00000BBL)

#define WLAN_STATUS_MULTICAST_FULL              ((WLAN_STATUS) 0xC0010009L)
#define WLAN_STATUS_INVALID_PACKET              ((WLAN_STATUS) 0xC001000FL)
#define WLAN_STATUS_ADAPTER_NOT_READY           ((WLAN_STATUS) 0xC0010011L)
#define WLAN_STATUS_NOT_INDICATING              ((WLAN_STATUS) 0xC0010013L)
#define WLAN_STATUS_INVALID_LENGTH              ((WLAN_STATUS) 0xC0010014L)
#define WLAN_STATUS_INVALID_DATA                ((WLAN_STATUS) 0xC0010015L)
#define WLAN_STATUS_BUFFER_TOO_SHORT            ((WLAN_STATUS) 0xC0010016L)

#define WLAN_STATUS_BWCS_UPDATE            ((WLAN_STATUS) 0xC0010017L)

#define WLAN_STATUS_CONNECT_INDICATION          ((WLAN_STATUS) 0xC0010018L)


/* NIC status flags */
#define ADAPTER_FLAG_HW_ERR                     0x00400000

/* Type Length */
#define TL_IPV4     0x0008
#define TL_IPV6     0xDD86


/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
/* Type definition for GLUE_INFO structure */
typedef struct _GLUE_INFO_T     GLUE_INFO_T, *P_GLUE_INFO_T;

/* Type definition for WLAN STATUS */
typedef UINT_32                 WLAN_STATUS, *P_WLAN_STATUS;

/* Type definition for ADAPTER structure */
typedef struct _ADAPTER_T       ADAPTER_T, *P_ADAPTER_T;

/* Type definition for MESSAGE HEADER structure */
typedef struct _MSG_HDR_T       MSG_HDR_T, *P_MSG_HDR_T;

/* Type definition for Pointer to OS Native Packet */
typedef void                    *P_NATIVE_PACKET;

/* Type definition for STA_RECORD_T structure to handle the connectivity and packet reception
 * for a particular STA.
 */
typedef struct _STA_RECORD_T    STA_RECORD_T, *P_STA_RECORD_T, **PP_STA_RECORD_T;

/* CMD_INFO_T is used by Glue Layer to send a cluster of Command(OID) information to
 * the TX Path to reduce the parameters of a function call.
 */
typedef struct _CMD_INFO_T      CMD_INFO_T, *P_CMD_INFO_T;

/* Following typedef should be removed later, because Glue Layer should not
 * be aware of following data type.
 */
typedef struct _SW_RFB_T        SW_RFB_T, *P_SW_RFB_T, **PP_SW_RFB_T;

typedef struct _MSDU_INFO_T     MSDU_INFO_T, *P_MSDU_INFO_T;

typedef struct _REG_ENTRY_T     REG_ENTRY_T, *P_REG_ENTRY_T;

/* IST handler definition */
typedef VOID (*IST_EVENT_FUNCTION)(P_ADAPTER_T);

/* Type definition for function pointer of timer handler */
typedef VOID (*PFN_TIMER_CALLBACK)(IN P_GLUE_INFO_T);


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
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#endif /* _TYPEDEF_H */


