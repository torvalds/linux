/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/mgmt/hem_mbox.h#2 $
*/

/*! \file   hem_mbox.h
    \brief

*/



/*
** $Log: hem_mbox.h $
** 
** 07 26 2012 yuche.tsai
** [ALPS00324337] [ALPS.JB][Hot-Spot] Driver update for Hot-Spot
** Update driver code of ALPS.JB for hot-spot.
** 
** 07 19 2012 yuche.tsai
** NULL
** Code update for JB.
 *
 * 03 02 2012 terry.wu
 * NULL
 * Sync CFG80211 modification from branch 2,2.
 *
 * 07 18 2011 cp.wu
 * [WCXRP00000858] [MT5931][Driver][Firmware] Add support for scan to search for more than one SSID in a single scanning request
 * add framework in driver domain for supporting new SCAN_REQ_V2 for more than 1 SSID support as well as uProbeDelay in NDIS 6.x driver model
 *
 * 06 07 2011 yuche.tsai
 * [WCXRP00000696] [Volunteer Patch][MT6620][Driver] Infinite loop issue when RX invitation response.[WCXRP00000763] [Volunteer Patch][MT6620][Driver] RX Service Discovery Frame under AP mode Issue
 * Add invitation support.
 *
 * 06 02 2011 cp.wu
 * [WCXRP00000681] [MT5931][Firmware] HIF code size reduction
 * eliminate unused parameters for SAA-FSM
 *
 * 01 26 2011 cm.chang
 * [WCXRP00000395] [MT6620 Wi-Fi][Driver][FW] Search STA_REC with additional net type index argument
 * Allocate system RAM if fixed message or mgmt buffer is not available
 *
 * 11 08 2010 cm.chang
 * [WCXRP00000169] [MT6620 Wi-Fi][Driver][FW] Remove unused CNM recover message ID
 * Remove CNM channel reover message ID
 *
 * 09 16 2010 cm.chang
 * NULL
 * Remove unused message ID
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 30 2010 cp.wu
 * NULL
 * eliminate klockwork errors
 *
 * 08 25 2010 george.huang
 * NULL
 * update OID/ registry control path for PM related settings
 *
 * 08 23 2010 chinghwa.yu
 * NULL
 * Update for BOW.
 *
 * 08 16 2010 cp.wu
 * NULL
 * add interface for RLM to trigger OBSS-SCAN.
 *
 * 08 11 2010 yuche.tsai
 * NULL
 * Add some message ID for P2P FSM under provisioning phase.
 *
 * 08 11 2010 yuche.tsai
 * NULL
 * Add Message Event ID for P2P Module.
 *
 * 08 05 2010 yuche.tsai
 * NULL
 * Check-in P2P Device Discovery Feature.
 *
 * 08 04 2010 cp.wu
 * NULL
 * remove unused mailbox message definitions.
 *
 * 08 02 2010 yuche.tsai
 * NULL
 * P2P Group Negotiation Code Check in.
 *
 * 07 19 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * message table should not be commented out by compilation option without modifying header file
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 08 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Rename MID_MNY_CNM_CH_RELEASE to MID_MNY_CNM_CH_ABORT
 *
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * AIS-FSM integration with CNM channel request messages
 *
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * implementation of DRV-SCN and related mailbox message handling.
 *
 * 07 01 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Modify CNM message handler for new flow
 *
 * 06 18 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Provide cnmMgtPktAlloc() and alloc/free function of msg/buf
 *
 * 06 14 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * restore utility function invoking via hem_mbox to direct calls
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * auth.c is migrated.
 *
 * 06 10 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add buildable & linkable ais_fsm.c
 *
 * related reference are still waiting to be resolved
 *
 * 06 09 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add definitions for module migration.
 *
 * 06 08 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * hem_mbox is migrated.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * merge cnm_scan.h and hem_mbox.h
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Add Power Management - Legacy PS-POLL support.
 *
 * 04 29 2010 tehuang.liu
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Removed MID_RXM_MQM_QOS_ACTION_FRAME
 *
 * 04 29 2010 tehuang.liu
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Removed MID_RXM_MQM_BA_ACTION_FRAME
 *
 * 03 30 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support 2.4G OBSS scan
 *
 * 03 16 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Add AdHoc Mode
 *
 * 03 10 2010 kevin.huang
 * [BORA00000654][WIFISYS][New Feature] CNM Module - Ch Manager Support
 *
 *  *  *  *  * Add Channel Manager for arbitration of JOIN and SCAN Req
 *
 * 03 05 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Develop partial DPD code
 *
 * 02 11 2010 tehuang.liu
 * [BORA00000569][WIFISYS] Phase 2 Integration Test
 * Added MID_RXM_MQM_QOS_ACTION_FRAME for RXM to indicate QoS Action frames to MQM
 *
 * 01 11 2010 kevin.huang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add Deauth and Disassoc Handler
 *
 * Dec 7 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Rename the parameter of mboxDummy()
 *
 * Dec 2 2009 MTK02468
 * [BORA00000337] To check in codes for FPGA emulation
 * Added MID_RXM_MQM_BA_ACTION_FRAME
 *
 * Nov 24 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Remove Dummy MSG ID
 *
 * Nov 23 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add JOIN REQ related MSG ID
 *
 * Nov 16 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add AIS ABORT MSG ID
 *
 * Nov 5 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add SCN MSG IDs
 *
 * Oct 28 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 *
*/

#ifndef _HEM_MBOX_H
#define _HEM_MBOX_H

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

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
/* Message IDs */
typedef enum _ENUM_MSG_ID_T {
    MID_MNY_CNM_CH_REQ,                 /* MANY notify CNM to obtain channel privilege */
    MID_MNY_CNM_CH_ABORT,               /* MANY notify CNM to abort/release channel privilege */

    MID_CNM_AIS_CH_GRANT,               /* CNM notify AIS for indicating channel granted */
    MID_CNM_P2P_CH_GRANT,               /* CNM notify P2P for indicating channel granted */
    MID_CNM_BOW_CH_GRANT,               /* CNM notify BOW for indicating channel granted */

    /*--------------------------------------------------*/
    /* SCN Module Mailbox Messages                      */
    /*--------------------------------------------------*/
    MID_AIS_SCN_SCAN_REQ,               /* AIS notify SCN for starting scan */
    MID_AIS_SCN_SCAN_REQ_V2,            /* AIS notify SCN for starting scan with multiple SSID support */
    MID_AIS_SCN_SCAN_CANCEL,            /* AIS notify SCN for cancelling scan */
    MID_P2P_SCN_SCAN_REQ,               /* P2P notify SCN for starting scan */
    MID_P2P_SCN_SCAN_REQ_V2,            /* P2P notify SCN for starting scan with multiple SSID support */
    MID_P2P_SCN_SCAN_CANCEL,            /* P2P notify SCN for cancelling scan */
    MID_BOW_SCN_SCAN_REQ,               /* BOW notify SCN for starting scan */
    MID_BOW_SCN_SCAN_REQ_V2,            /* BOW notify SCN for starting scan with multiple SSID support */
    MID_BOW_SCN_SCAN_CANCEL,            /* BOW notify SCN for cancelling scan */
    MID_RLM_SCN_SCAN_REQ,               /* RLM notify SCN for starting scan (OBSS-SCAN) */
    MID_RLM_SCN_SCAN_REQ_V2,            /* RLM notify SCN for starting scan (OBSS-SCAN) with multiple SSID support */
    MID_RLM_SCN_SCAN_CANCEL,            /* RLM notify SCN for cancelling scan (OBSS-SCAN)*/
    MID_SCN_AIS_SCAN_DONE,              /* SCN notify AIS for scan completion */
    MID_SCN_P2P_SCAN_DONE,              /* SCN notify P2P for scan completion */
    MID_SCN_BOW_SCAN_DONE,              /* SCN notify BOW for scan completion */
    MID_SCN_RLM_SCAN_DONE,              /* SCN notify RLM for scan completion (OBSS-SCAN) */

    /*--------------------------------------------------*/
    /* AIS Module Mailbox Messages                      */
    /*--------------------------------------------------*/
    MID_OID_AIS_FSM_JOIN_REQ,           /* OID/IOCTL notify AIS for join */
    MID_OID_AIS_FSM_ABORT,              /* OID/IOCTL notify AIS for abort */
    MID_AIS_SAA_FSM_START,              /* AIS notify SAA for Starting authentication/association fsm */
    MID_AIS_SAA_FSM_ABORT,              /* AIS notify SAA for Aborting authentication/association fsm */
    MID_SAA_AIS_JOIN_COMPLETE,          /* SAA notify AIS for indicating join complete */

#if CFG_ENABLE_BT_OVER_WIFI
    /*--------------------------------------------------*/
    /* BOW Module Mailbox Messages                      */
    /*--------------------------------------------------*/
    MID_BOW_SAA_FSM_START,              /* BOW notify SAA for Starting authentication/association fsm */
    MID_BOW_SAA_FSM_ABORT,              /* BOW notify SAA for Aborting authentication/association fsm */
    MID_SAA_BOW_JOIN_COMPLETE,          /* SAA notify BOW for indicating join complete */
#endif

#if CFG_ENABLE_WIFI_DIRECT
    /*--------------------------------------------------*/
    /* P2P Module Mailbox Messages                      */
    /*--------------------------------------------------*/
    MID_P2P_SAA_FSM_START,              /* P2P notify SAA for Starting authentication/association fsm */
    MID_P2P_SAA_FSM_ABORT,              /* P2P notify SAA for Aborting authentication/association fsm */
    MID_SAA_P2P_JOIN_COMPLETE,          /* SAA notify P2P for indicating join complete */

    MID_MNY_P2P_FUN_SWITCH,             /* Enable P2P FSM. */
    MID_MNY_P2P_DEVICE_DISCOVERY,       /* Start device discovery. */
    MID_MNY_P2P_CONNECTION_REQ,         /* Connection request. */
    MID_MNY_P2P_CONNECTION_ABORT,       /* Abort connection request, P2P FSM return to IDLE. */
    MID_MNY_P2P_BEACON_UPDATE,
    MID_MNY_P2P_STOP_AP,
    MID_MNY_P2P_CHNL_REQ,
    MID_MNY_P2P_CHNL_ABORT,
    MID_MNY_P2P_MGMT_TX,
    MID_MNY_P2P_GROUP_DISSOLVE,
    MID_MNY_P2P_MGMT_FRAME_REGISTER,
    MID_MNY_P2P_NET_DEV_REGISTER,
    MID_MNY_P2P_START_AP,
    MID_MNY_P2P_MGMT_FRAME_UPDATE,
#if CFG_SUPPORT_WFD
    MID_MNY_P2P_WFD_CFG_UPDATE,
#endif    
#endif

#if CFG_SUPPORT_ADHOC
    MID_SCN_AIS_FOUND_IBSS,             /* SCN notify AIS that an IBSS Peer has been found and can merge into */
#endif /* CFG_SUPPORT_ADHOC */

    MID_SAA_AIS_FSM_ABORT,              /* SAA notify AIS for indicating deauthentication/disassociation */

    MID_TOTAL_NUM
} ENUM_MSG_ID_T, *P_ENUM_MSG_ID_T;

/* Message header of inter-components */
struct _MSG_HDR_T {
    LINK_ENTRY_T    rLinkEntry;
    ENUM_MSG_ID_T   eMsgId;
};

typedef VOID (*PFN_MSG_HNDL_FUNC)(P_ADAPTER_T, P_MSG_HDR_T);

typedef struct _MSG_HNDL_ENTRY {
    ENUM_MSG_ID_T       eMsgId;
    PFN_MSG_HNDL_FUNC   pfMsgHndl;
} MSG_HNDL_ENTRY_T, *P_MSG_HNDL_ENTRY_T;

typedef enum _EUNM_MSG_SEND_METHOD_T {
    MSG_SEND_METHOD_BUF = 0,    /* Message is put in the queue and will be
                                   executed when mailbox is checked. */
    MSG_SEND_METHOD_UNBUF       /* The handler function is called immediately
                                   in the same context of the sender */
} EUNM_MSG_SEND_METHOD_T, *P_EUNM_MSG_SEND_METHOD_T;


typedef enum _ENUM_MBOX_ID_T {
    MBOX_ID_0 = 0,
    MBOX_ID_TOTAL_NUM
} ENUM_MBOX_ID_T, *P_ENUM_MBOX_ID_T;

/* Define Mailbox structure */
typedef struct _MBOX_T {
    LINK_T          rLinkHead;
} MBOX_T, *P_MBOX_T;

typedef struct _MSG_SAA_FSM_START_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    UINT_8          ucSeqNum;
    P_STA_RECORD_T  prStaRec;
} MSG_SAA_FSM_START_T, *P_MSG_SAA_FSM_START_T;

typedef struct _MSG_SAA_FSM_COMP_T {
    MSG_HDR_T       rMsgHdr;        /* Must be the first member */
    UINT_8          ucSeqNum;
    WLAN_STATUS     rJoinStatus;
    P_STA_RECORD_T  prStaRec;
    P_SW_RFB_T      prSwRfb;
} MSG_SAA_FSM_COMP_T, *P_MSG_SAA_FSM_COMP_T;

typedef struct _MSG_SAA_FSM_ABORT_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    UINT_8          ucSeqNum;
    P_STA_RECORD_T  prStaRec;
} MSG_SAA_FSM_ABORT_T, *P_MSG_SAA_FSM_ABORT_T;

typedef struct _MSG_CONNECTION_ABORT_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    UINT_8          ucNetTypeIndex;
} MSG_CONNECTION_ABORT_T, *P_MSG_CONNECTION_ABORT_T;



/* specific message data types */
typedef MSG_SAA_FSM_START_T MSG_JOIN_REQ_T, *P_MSG_JOIN_REQ_T;
typedef MSG_SAA_FSM_COMP_T MSG_JOIN_COMP_T, *P_MSG_JOIN_COMP_T;
typedef MSG_SAA_FSM_ABORT_T MSG_JOIN_ABORT_T, *P_MSG_JOIN_ABORT_T;


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
VOID
mboxSetup (
    IN P_ADAPTER_T  prAdapter,
    IN ENUM_MBOX_ID_T  eMboxId
    );


VOID
mboxSendMsg (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_MBOX_ID_T eMboxId,
    IN P_MSG_HDR_T prMsg,
    IN EUNM_MSG_SEND_METHOD_T eMethod
    );

VOID
mboxRcvAllMsg (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_MBOX_ID_T eMboxId
    );

VOID
mboxInitialize (
    IN P_ADAPTER_T prAdapter
    );

VOID
mboxDestroy (
    IN P_ADAPTER_T prAdapter
    );

VOID
mboxDummy (
    IN P_ADAPTER_T prAdapter,
    P_MSG_HDR_T prMsgHdr
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _HEM_MBOX_H */


