/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/mgmt/sec_fsm.h#1 $
*/

/*! \file   sec_fsm.h
    \brief  Declaration of functions and finite state machine for SECURITY Module.

    Function declaration for privacy.c and SEC_STATE for SECURITY FSM.
*/



/*
** $Log: sec_fsm.h $
 *
 * 09 29 2010 wh.su
 * [WCXRP00000072] [MT6620 Wi-Fi][Driver] Fix TKIP Counter Measure EAPoL callback register issue
 * [MT6620 Wi-Fi][Driver] Fix TKIP Counter Measure EAPoL callback register issue.
 *
 * 09 21 2010 kevin.huang
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * Eliminate Linux Compile Warning
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 20 2010 wh.su
 * NULL
 * adding the eapol callback setting.
 *
 * 08 19 2010 wh.su
 * NULL
 * adding the tx pkt call back handle for countermeasure.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 21 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * modify some code for concurrent network.
 *
 * 06 19 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * consdier the concurrent network setting.
 *
 * 03 04 2010 wh.su
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Code refine, and remove non-used code.
 *
 * 03 03 2010 wh.su
 * [BORA00000637][MT6620 Wi-Fi] [Bug] WPA2 pre-authentication timer not correctly initialize
 * Fixed the pre-authentication timer not correctly init issue, and modify the security related callback function prototype.
 *
 * 03 01 2010 wh.su
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Refine the variable and parameter for security.
 *
 * 12 18 2009 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * .
 *
 * Dec 7 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * fixed the deauth Tx done callback parameter
 *
 * Dec 4 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adding the reference function declaration
 *
 * Dec 3 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * delete non-used code
 *
 * Dec 1 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adjust the function prototype
 *
 * Nov 23 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adjust the function declaration
 *
 * Nov 19 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adding the security variable
 *
 * Nov 18 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 *
**  \main\maintrunk.MT5921\14 2009-04-06 15:35:47 GMT mtk01088
**  add the variable to set the disable AP selection for privacy check, for wps open networking.
**  \main\maintrunk.MT5921\13 2008-11-19 11:46:01 GMT mtk01088
**  rename some variable with pre-fix to avoid the misunderstanding
**  \main\maintrunk.MT5921\12 2008-08-28 20:37:11 GMT mtk01088
**  remove non-used code
**
**  \main\maintrunk.MT5921\11 2008-03-18 09:51:52 GMT mtk01088
**  Add function declaration for timer to indicate pmkid candidate
**  \main\maintrunk.MT5921\10 2008-02-29 15:12:08 GMT mtk01088
**  add variable for sw port control
**  \main\maintrunk.MT5921\9 2008-02-29 12:37:30 GMT mtk01088
**  rename the security related function declaration
**  \main\maintrunk.MT5921\8 2007-12-27 13:59:08 GMT mtk01088
**  adjust the wlan table and sec fsm init timing
**  \main\maintrunk.MT5921\7 2007-11-20 10:39:49 GMT mtk01088
**  add function timer for wait EAPoL Error timeout
**  \main\maintrunk.MT5921\6 2007-11-06 20:39:08 GMT mtk01088
**  rename the counter measure timer
**  \main\maintrunk.MT5921\5 2007-11-06 20:14:31 GMT mtk01088
**  add a abort function
** Revision 1.5  2007/07/16 02:33:42  MTK01088
** change the ENUM declaration structure prefix from r to e
**
** Revision 1.4  2007/07/09 06:23:10  MTK01088
** update
**
** Revision 1.3  2007/07/04 10:09:04  MTK01088
** adjust the state for security fsm
** change function name
**
** Revision 1.2  2007/07/03 08:13:22  MTK01088
** change the sec fsm state
** add the event for sec fsm
**
** Revision 1.1  2007/06/27 06:20:35  MTK01088
** add the sec fsm header file
**
**
*/
#ifndef _SEC_FSM_H
#define _SEC_FSM_H

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

/* TKIP CounterMeasure interval for Rejoin to Network. */
#define COUNTER_MEASURE_TIMEOUT_INTERVAL_SEC        60

/* Timeout to wait the EAPoL Error Report frame Send out. */
#define EAPOL_REPORT_SEND_TIMEOUT_INTERVAL_SEC       1

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

typedef UINT_32             SEC_STATUS, *P_SEC_STATUS;

#if 0
/* WPA2 PMKID candicate structure */
typedef struct _PMKID_CANDICATE_T {
    UINT_8              aucBssid[MAC_ADDR_LEN];    /* MAC address */
    UINT_32             u4PreAuthFlags;
} PMKID_CANDICATE_T, *P_PMKID_CANDICATE_T;
#endif

typedef SEC_STATUS (*PFN_SEC_FSM_STATE_HANDLER)(VOID);

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
#define SEC_STATE_TRANSITION_FLAG   fgIsTransition
#define SEC_NEXT_STATE_VAR          eNextState

#define SEC_STATE_TRANSITION(prAdapter, prSta, eFromState, eToState) \
        { secFsmTrans_ ## eFromState ## _to_ ## eToState(prAdapter, prSta); \
          SEC_NEXT_STATE_VAR = SEC_STATE_ ## eToState; \
          SEC_STATE_TRANSITION_FLAG = (BOOLEAN)TRUE; \
        }

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*--------------------------------------------------------------*/
/* Routines to handle the sec check                             */
/*--------------------------------------------------------------*/
/***** Routines in sec_fsm.c *****/
VOID
secFsmInit(
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    );

VOID
secFsmEventInit(
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    );

VOID
secFsmEventStart(
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    );

VOID
secFsmEventAbort(
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    );

BOOLEAN
secFsmEventPTKInstalled(
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    );

VOID
secFsmEvent2ndEapolTx(
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    );

VOID
secFsmEvent4ndEapolTxDone(
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    );

VOID
secFsmEventEapolTxDone (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prStaRec,
    IN ENUM_TX_RESULT_CODE_T  rTxDoneStatus
    );

VOID
secFsmEventEapolTxTimeout (
    IN P_ADAPTER_T            prAdapter,
    IN UINT_32                u4Parm
    );

VOID
secFsmEventDeauthTxDone(
    IN P_ADAPTER_T          prAdapter,
    IN P_MSDU_INFO_T        prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T  rTxDoneStatus
    );

VOID
secFsmEventStartCounterMeasure(
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    );

VOID
secFsmEventEndOfCounterMeasure(
    IN P_ADAPTER_T          prAdapter,
    IN UINT_32              u4Parm
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#endif /* _SEC_FSM_H */


