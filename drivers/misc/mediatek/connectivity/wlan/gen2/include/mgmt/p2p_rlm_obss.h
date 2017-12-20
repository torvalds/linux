/*
** Id: //Department/DaVinci/TRUNK/WiFi_P2P_Driver/include/mgmt/p2p_rlm_obss.h#1
*/

/*! \file   "rlm_obss.h"
    \brief
*/

#ifndef _P2P_RLM_OBSS_H
#define _P2P_RLM_OBSS_H

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
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

VOID rlmRspGenerateObssScanIE(P_ADAPTER_T prAdapter, P_MSDU_INFO_T prMsduInfo);

VOID rlmProcessPublicAction(P_ADAPTER_T prAdapter, P_SW_RFB_T prSwRfb);

VOID rlmProcessHtAction(P_ADAPTER_T prAdapter, P_SW_RFB_T prSwRfb);

VOID rlmHandleObssStatusEventPkt(P_ADAPTER_T prAdapter, P_EVENT_AP_OBSS_STATUS_T prObssStatus);

UINT_8 rlmObssChnlLevel(P_BSS_INFO_T prBssInfo, ENUM_BAND_T eBand, UINT_8 ucPriChannel, ENUM_CHNL_EXT_T eExtend);

VOID rlmObssScanExemptionRsp(P_ADAPTER_T prAdapter, P_BSS_INFO_T prBssInfo, P_SW_RFB_T prSwRfb);

#endif
