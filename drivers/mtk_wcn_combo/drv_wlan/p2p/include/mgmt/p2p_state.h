
#ifndef _P2P_STATE_H
#define _P2P_STATE_H

BOOLEAN
p2pStateInit_IDLE(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN P_BSS_INFO_T prP2pBssInfo,
    OUT P_ENUM_P2P_STATE_T peNextState
    );


VOID
p2pStateAbort_IDLE(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN ENUM_P2P_STATE_T eNextState
    );

VOID
p2pStateInit_SCAN(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo
    );

VOID
p2pStateAbort_SCAN(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN ENUM_P2P_STATE_T eNextState
    );

VOID
p2pStateInit_AP_CHANNEL_DETECT(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo
    );

VOID
p2pStateAbort_AP_CHANNEL_DETECT(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo,
    IN ENUM_P2P_STATE_T eNextState
    );

VOID
p2pStateInit_CHNL_ON_HAND(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo
    );

VOID
p2pStateAbort_CHNL_ON_HAND(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN ENUM_P2P_STATE_T eNextState
    );


VOID
p2pStateAbort_REQING_CHANNEL(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN ENUM_P2P_STATE_T eNextState
    );


VOID
p2pStateInit_GC_JOIN(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN P_P2P_JOIN_INFO_T prJoinInfo,
    IN P_BSS_DESC_T prBssDesc
    );

VOID
p2pStateAbort_GC_JOIN(
    IN P_ADAPTER_T prAdapter,
    IN P_P2P_FSM_INFO_T prP2pFsmInfo,
    IN P_P2P_JOIN_INFO_T prJoinInfo,
    IN ENUM_P2P_STATE_T eNextState
    );

#endif

