/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/mgmt/aa_fsm.h#1 $
*/

/*! \file   aa_fsm.h
    \brief  Declaration of functions and finite state machine for SAA/AAA Module.

    Declaration of functions and finite state machine for SAA/AAA Module.
*/



/*
** $Log: aa_fsm.h $
 *
 * 10 13 2011 cp.wu
 * [MT6620 Wi-Fi][Driver] Reduce join failure count limit to 2 for faster re-join for other BSS 
 * 1. short join failure count limit to 2
 * 2. treat join timeout as kind of join failure as well
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * refine TX-DONE callback.
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * 1) migrate assoc.c.
 * 2) add ucTxSeqNum for tracking frames which needs TX-DONE awareness
 * 3) add configuration options for CNM_MEM and RSN modules
 * 4) add data path for management frames
 * 5) eliminate rPacketInfo of MSDU_INFO_T
 *
 * 06 10 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * add buildable & linkable ais_fsm.c
 * 
 * related reference are still waiting to be resolved
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * add aa_fsm.h, ais_fsm.h, bss.h, mib.h and scan.h.
 *
 * 03 10 2010 kevin.huang
 * [BORA00000654][WIFISYS][New Feature] CNM Module - Ch Manager Support 
 * 
 * Add Channel Manager for arbitration of JOIN and SCAN Req
 *
 * 02 04 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support 
 * Add AAA Module Support, Revise Net Type to Net Type Index for array lookup
 *
 * 01 11 2010 kevin.huang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time 
 * Add Deauth and Disassoc Handler
 *
 * Nov 24 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Revise MGMT Handler with Retain Status
 *
 * Nov 23 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * 
*/


#ifndef _AA_FSM_H
#define _AA_FSM_H

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
/* Retry interval for retransmiting authentication-request MMPDU. */
#define TX_AUTHENTICATION_RETRY_TIMEOUT_TU          100 // TU.

/* Retry interval for retransmiting association-request MMPDU. */
#define TX_ASSOCIATION_RETRY_TIMEOUT_TU             100 // TU.

/* Wait for a response to a transmitted authentication-request MMPDU. */
#define DOT11_AUTHENTICATION_RESPONSE_TIMEOUT_TU    512 // TU.

/* Wait for a response to a transmitted association-request MMPDU. */
#define DOT11_ASSOCIATION_RESPONSE_TIMEOUT_TU       512 // TU.

/* The maximum time to wait for JOIN process complete. */
#define JOIN_FAILURE_TIMEOUT_BEACON_INTERVAL        20 // Beacon Interval, 20 * 100TU = 2 sec.

/* Retry interval for next JOIN request. */
#define JOIN_RETRY_INTERVAL_SEC                     10 // Seconds

/* Maximum Retry Count for accept a JOIN request. */
#define JOIN_MAX_RETRY_FAILURE_COUNT                2 // Times

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef enum _ENUM_AA_STATE_T {
    AA_STATE_IDLE = 0,
    SAA_STATE_SEND_AUTH1,
    SAA_STATE_WAIT_AUTH2,
    SAA_STATE_SEND_AUTH3,
    SAA_STATE_WAIT_AUTH4,
    SAA_STATE_SEND_ASSOC1,
    SAA_STATE_WAIT_ASSOC2,
    AAA_STATE_SEND_AUTH2,
    AAA_STATE_SEND_AUTH4, // We may not use, because P2P GO didn't support WEP and 11r
    AAA_STATE_SEND_ASSOC2,
    AA_STATE_RESOURCE, // A state for debugging the case of out of msg buffer. 
    AA_STATE_NUM
} ENUM_AA_STATE_T;

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
/*----------------------------------------------------------------------------*/
/* Routines in saa_fsm.c                                                      */
/*----------------------------------------------------------------------------*/
VOID
saaFsmSteps (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec,
    IN ENUM_AA_STATE_T eNextState,
    IN P_SW_RFB_T prRetainedSwRfb
    );

WLAN_STATUS
saaFsmSendEventJoinComplete (
    IN P_ADAPTER_T prAdapter,
    WLAN_STATUS rJoinStatus,
    P_STA_RECORD_T prStaRec,
    P_SW_RFB_T prSwRfb
    );

VOID
saaFsmRunEventStart (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );
    
WLAN_STATUS
saaFsmRunEventTxDone (
    IN P_ADAPTER_T              prAdapter,
    IN P_MSDU_INFO_T            prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T    rTxDoneStatus
    );

VOID
saaFsmRunEventTxReqTimeOut (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    );

VOID
saaFsmRunEventRxRespTimeOut (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    );

VOID
saaFsmRunEventRxAuth (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );

WLAN_STATUS
saaFsmRunEventRxAssoc (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );

WLAN_STATUS
saaFsmRunEventRxDeauth (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );

WLAN_STATUS
saaFsmRunEventRxDisassoc (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );
    
VOID
saaFsmRunEventAbort (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

/*----------------------------------------------------------------------------*/
/* Routines in aaa_fsm.c                                                      */
/*----------------------------------------------------------------------------*/
VOID
aaaFsmRunEventRxAuth (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );

WLAN_STATUS
aaaFsmRunEventRxAssoc (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );

WLAN_STATUS
aaaFsmRunEventTxDone (
    IN P_ADAPTER_T              prAdapter,
    IN P_MSDU_INFO_T            prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T    rTxDoneStatus
    );
  
/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _AA_FSM_H */




