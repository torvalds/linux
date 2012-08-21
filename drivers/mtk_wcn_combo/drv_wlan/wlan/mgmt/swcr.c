/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_2/mgmt/swcr.c#2 $
*/

/*! \file   "swcr.c"
    \brief

*/

/*******************************************************************************
* Copyright (c) 2009 MediaTek Inc.
*
* All rights reserved. Copying, compilation, modification, distribution
* or any other use whatsoever of this material is strictly prohibited
* except in accordance with a Software License Agreement with
* MediaTek Inc.
********************************************************************************
*/

/*******************************************************************************
* LEGAL DISCLAIMER
*
* BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND
* AGREES THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK
* SOFTWARE") RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE
* PROVIDED TO BUYER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY
* DISCLAIMS ANY AND ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT
* LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
* PARTICULAR PURPOSE OR NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE
* ANY WARRANTY WHATSOEVER WITH RESPECT TO THE SOFTWARE OF ANY THIRD PARTY
* WHICH MAY BE USED BY, INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK
* SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY
* WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE
* FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S SPECIFICATION OR TO
* CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
* BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
* LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL
* BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT
* ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY
* BUYER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
* THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
* WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT
* OF LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING
* THEREOF AND RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN
* FRANCISCO, CA, UNDER THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE
* (ICC).
********************************************************************************
*/

/*
** $Log: swcr.c $
 *
 * 01 15 2012 yuche.tsai
 * NULL
 * Fix wrong basic rate issue.
 *
 * 11 22 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * keep debug counter setting after wake up.
 *
 * 11 19 2011 eddie.chen
 * [WCXRP00001096] [MT6620 Wi-Fi][Driver/FW] Enhance the log function (xlog)
 * Fix initialize the prTxCtrol
 *
 * 11 19 2011 eddie.chen
 * [WCXRP00001096] [MT6620 Wi-Fi][Driver/FW] Enhance the log function (xlog)
 * Add xlog for tx
 *
 * 11 18 2011 eddie.chen
 * [WCXRP00001096] [MT6620 Wi-Fi][Driver/FW] Enhance the log function (xlog)
 * Fix xlog format to hex format
 *
 * 11 15 2011 cm.chang
 * NULL
 * Fix compiling warning
 *
 * 11 11 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * fix debug counters of rx in driver.
 *
 * 11 11 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * reform debug formt.
 *
 * 11 11 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * add txbcn count into xlog.
 *
 * 11 10 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * add debug counters of ar and bb for xlog.
 *
 * 11 10 2011 eddie.chen
 * [WCXRP00001096] [MT6620 Wi-Fi][Driver/FW] Enhance the log function (xlog)
 * Modify the QM xlog level and remove LOG_FUNC.
 *
 * 11 08 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * add Xlog for PS.
 *
 * 11 04 2011 tsaiyuan.hsu
 * [WCXRP00001083] [MT6620 Wi-Fi][DRV]] dump debug counter or frames when debugging is triggered
 * add debug counters and periodically dump counters for Xlog debugging.
 *
 * 11 03 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * change the DBGLOG for "\n" and "\r\n". LABEL to LOUD for XLOG
 *
 * 06 27 2011 tsaiyuan.hsu
 * [WCXRP00000816] [MT6620 Wi-Fi][Driver] add control to enable rx data dump or not
 * add control to enable rx data dump by packet type.
 *
 * 05 11 2011 eddie.chen
 * [WCXRP00000709] [MT6620 Wi-Fi][Driver] Check free number before copying broadcast packet
 * Fix dest type when GO packet copying.
 *
 * 05 09 2011 eddie.chen
 * [WCXRP00000709] [MT6620 Wi-Fi][Driver] Check free number before copying broadcast packet
 * Check free number before copying broadcast packet.
 *
 * 04 14 2011 eddie.chen
 * [WCXRP00000603] [MT6620 Wi-Fi][DRV] Fix Klocwork warning
 * Check the SW RFB free. Fix the compile warning..
 *
 * 04 12 2011 eddie.chen
 * [WCXRP00000617] [MT6620 Wi-Fi][DRV/FW] Fix for sigma
 * Fix the sta index in processing security frame
 * Simple flow control for TC4 to avoid mgt frames for PS STA to occupy the TC4
 * Add debug message.
 *
 * 03 28 2011 eddie.chen
 * [WCXRP00000603] [MT6620 Wi-Fi][DRV] Fix Klocwork warning
 * Fix Klocwork warning.
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 03 15 2011 eddie.chen
 * [WCXRP00000554] [MT6620 Wi-Fi][DRV] Add sw control debug counter
 * Add sw debug counter for QM.
 *
 * 01 11 2011 eddie.chen
 * [WCXRP00000322] Add WMM IE in beacon,
Add per station flow control when STA is in PS

 * Add swcr for test.
 *
*
*/


/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "precomp.h"

#if CFG_SUPPORT_SWCR

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wformat"
#endif
/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/
#if 0
extern SWCR_MAP_ENTRY_T g_arRlmArSwCrMap[];
SWCR_MOD_MAP_ENTRY_T g_arSwCrAllMaps[] = {
    { SWCR_MAP_NUM(g_arRlmArSwCrMap), g_arRlmArSwCrMap},  /* 0x00nn */
    {0,NULL}
};
#endif

VOID swCtrlCmdCategory0(P_ADAPTER_T prAdapter, UINT_8 ucCate, UINT_8 ucAction, UINT_8 ucOpt0,UINT_8 ucOpt1);
VOID swCtrlCmdCategory1(P_ADAPTER_T prAdapter, UINT_8 ucCate, UINT_8 ucAction, UINT_8 ucOpt0,UINT_8 ucOpt1);
VOID testPsCmdCategory0(P_ADAPTER_T prAdapter, UINT_8 ucCate, UINT_8 ucAction, UINT_8 ucOpt0,UINT_8 ucOpt1);
VOID testPsCmdCategory1(P_ADAPTER_T prAdapter, UINT_8 ucCate, UINT_8 ucAction, UINT_8 ucOpt0,UINT_8 ucOpt1);
VOID swCtrlSwCr(P_ADAPTER_T prAdapter, UINT_8 ucRead, UINT_16 u2Addr, UINT_32 *pu4Data);

/* Support Debug */
VOID swCrDebugCheck(P_ADAPTER_T  prAdapter, P_CMD_SW_DBG_CTRL_T prCmdSwCtrl);
VOID swCrDebugCheckTimeout(
    IN P_ADAPTER_T prAdapter,
    UINT_32 u4Param);
VOID swCrDebugQuery(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );
VOID swCrDebugQueryTimeout(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo);

UINT_32 g_au4SwCr[SWCR_CR_NUM]; /*: 0: command other: data */

static TIMER_T g_rSwcrDebugTimer;
static BOOLEAN g_fgSwcrDebugTimer = FALSE;
static UINT_32 g_u4SwcrDebugCheckTimeout = 0;
static ENUM_SWCR_DBG_TYPE_T g_ucSwcrDebugCheckType = 0;
static UINT_32 g_u4SwcrDebugFrameDumpType = 0;

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/
#define TEST_PS 1

const static PFN_CMD_RW_T g_arSwCtrlCmd[] ={
    swCtrlCmdCategory0,
    swCtrlCmdCategory1
#if TEST_PS
    , testPsCmdCategory0
    , testPsCmdCategory1
#endif

};


const PFN_SWCR_RW_T g_arSwCrModHandle[] = {
    swCtrlSwCr,
    NULL
};


/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

enum {
    SWCTRL_MAGIC,
    SWCTRL_DEBUG,
    SWCTRL_WIFI_VAR,
    SWCTRL_ENABLE_INT,
    SWCTRL_DISABLE_INT,
    SWCTRL_TXM_INFO,
    SWCTRL_RXM_INFO,
    SWCTRL_DUMP_BSS,
    SWCTRL_QM_INFO,
    SWCTRL_DUMP_ALL_QUEUE_LEN,
    SWCTRL_DUMP_MEM,
    SWCTRL_TX_CTRL_INFO,
    SWCTRL_DUMP_QUEUE,
    SWCTRL_DUMP_QM_DBG_CNT,
    SWCTRL_QM_DBG_CNT,
    SWCTRL_RX_PKTS_DUMP,
    SWCTRL_CATA0_INDEX_NUM
};

enum {
    SWCTRL_STA_INFO,
    SWCTRL_DUMP_STA,
    SWCTRL_STA_QUE_INFO,
    SWCTRL_CATA1_INDEX_NUM
};



#if TEST_PS
enum {
    TEST_PS_MAGIC,
    TEST_PS_SETUP_BSS,
    TEST_PS_ENABLE_BEACON,
    TEST_PS_TRIGGER_BMC,
    TEST_PS_SEND_NULL,
    TEST_PS_BUFFER_BMC,
    TEST_PS_UPDATE_BEACON,
    TEST_PS_CATA0_INDEX_NUM
};

enum {
    TEST_PS_STA_PS,
    TEST_PS_STA_ENTER_PS,
    TEST_PS_STA_EXIT_PS,
    TEST_PS_STA_TRIGGER_PSPOLL,
    TEST_PS_STA_TRIGGER_FRAME,
    TEST_PS_CATA1_INDEX_NUM
};
#endif





#define _SWCTRL_MAGIC 0x66201642

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/


/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

void dumpQueue(P_ADAPTER_T prAdapter)
{

    P_TX_CTRL_T prTxCtrl;
    P_QUE_MGT_T prQM;
    P_GLUE_INFO_T           prGlueInfo;
    UINT_32 i;
    UINT_32 j;


    DEBUGFUNC("dumpQueue");

    prTxCtrl = &prAdapter->rTxCtrl;
    prQM = &prAdapter->rQM;
    prGlueInfo = prAdapter->prGlueInfo;

    for(i = TC0_INDEX; i<=TC5_INDEX; i++) {
        DBGLOG(SW4, INFO,( "TC %u\n",i));
        DBGLOG(SW4, INFO,( "Max %u Free %u\n",
                prTxCtrl->rTc.aucMaxNumOfBuffer[i], prTxCtrl->rTc.aucFreeBufferCount[i]));

        DBGLOG(SW4, INFO,("Average %u minReserved %u CurrentTcResource %u GuaranteedTcResource %u\n",
           QM_GET_TX_QUEUE_LEN(prAdapter, i),
           prQM->au4MinReservedTcResource[i],
           prQM->au4CurrentTcResource[i],
           prQM->au4GuaranteedTcResource[i]));

     }


    for(i = 0; i<NUM_OF_PER_STA_TX_QUEUES; i++) {
        DBGLOG(SW4, INFO,( "TC %u HeadStaIdx %u ForwardCount %u\n",i, prQM->au4HeadStaRecIndex[i],prQM->au4ForwardCount[i]));
    }

      DBGLOG(SW4, INFO,( "BMC or unknown TxQueue Len %u\n",prQM->arTxQueue[0].u4NumElem));
      DBGLOG(SW4, INFO,( "Pending %d\n",prGlueInfo->i4TxPendingFrameNum));
      DBGLOG(SW4, INFO,( "Pending Security %d\n",prGlueInfo->i4TxPendingSecurityFrameNum));
#if defined(LINUX)
   for(i=0;i<4;i++){
       for(j=0;j<CFG_MAX_TXQ_NUM;j++){
          DBGLOG(SW4, INFO,( "Pending Q[%u][%u] %d\n",i,j,prGlueInfo->ai4TxPendingFrameNumPerQueue[i][j]));
        }
    }
#endif

   DBGLOG(SW4, INFO,( " rFreeSwRfbList %u\n", prAdapter->rRxCtrl.rFreeSwRfbList.u4NumElem));
   DBGLOG(SW4, INFO,( " rReceivedRfbList %u\n", prAdapter->rRxCtrl.rReceivedRfbList.u4NumElem));
   DBGLOG(SW4, INFO,( " rIndicatedRfbList %u\n", prAdapter->rRxCtrl.rIndicatedRfbList.u4NumElem));
   DBGLOG(SW4, INFO,( " ucNumIndPacket %u\n", prAdapter->rRxCtrl.ucNumIndPacket));
   DBGLOG(SW4, INFO,( " ucNumRetainedPacket %u\n", prAdapter->rRxCtrl.ucNumRetainedPacket));


}


void dumpSTA(P_ADAPTER_T prAdapter, P_STA_RECORD_T prStaRec)
{
    UINT_8 ucWTEntry;
    UINT_32 i;
    P_BSS_INFO_T            prBssInfo;

    DEBUGFUNC("dumpSTA");

    ASSERT(prStaRec);
    ucWTEntry = prStaRec->ucWTEntry;

    prBssInfo = &prAdapter->rWifiVar.arBssInfo[prStaRec->ucNetTypeIndex];
    ASSERT(prBssInfo);

    DBGLOG(SW4, INFO,("Mac address: " MACSTR " Rcpi %u" "\n", MAC2STR(prStaRec->aucMacAddr),prStaRec->ucRCPI));

    DBGLOG(SW4, INFO,("Idx %u Wtbl %u Used %u State %u Bss Phy 0x%x Sta DesiredPhy 0x%x\n",
                prStaRec->ucIndex, ucWTEntry,
                prStaRec->fgIsInUse,prStaRec->ucStaState,
                prBssInfo->ucPhyTypeSet,
                prStaRec->ucDesiredPhyTypeSet));

    DBGLOG(SW4, INFO,("Sta Operation 0x%x  DesiredNontHtRateSet  0x%x Mcs 0x%x u2HtCapInfo 0x%x\n",
                prStaRec->u2OperationalRateSet,prStaRec->u2DesiredNonHTRateSet,prStaRec->ucMcsSet, prStaRec->u2HtCapInfo));


    for(i = 0; i<NUM_OF_PER_STA_TX_QUEUES; i++) {
        DBGLOG(SW4, INFO,( "TC %u Queue Len %u\n",i,prStaRec->arTxQueue[i].u4NumElem));
   }

    DBGLOG(SW4, INFO, ("BmpDeliveryAC %x\n",prStaRec->ucBmpDeliveryAC));
    DBGLOG(SW4, INFO, ("BmpTriggerAC  %x\n",prStaRec->ucBmpTriggerAC));
    DBGLOG(SW4, INFO, ("UapsdSpSupproted  %u\n",prStaRec->fgIsUapsdSupported));
    DBGLOG(SW4, INFO, ("IsQoS  %u\n",prStaRec->fgIsQoS));
    DBGLOG(SW4, INFO, ("AssocId %u\n",prStaRec->u2AssocId));

    DBGLOG(SW4, INFO, ("fgIsInPS %u\n",prStaRec->fgIsInPS));
    DBGLOG(SW4, INFO, ("ucFreeQuota %u\n",prStaRec->ucFreeQuota));
    DBGLOG(SW4, INFO, ("ucFreeQuotaForDelivery %u\n",prStaRec->ucFreeQuotaForDelivery));
    DBGLOG(SW4, INFO, ("ucFreeQuotaForNonDelivery %u\n",prStaRec->ucFreeQuotaForNonDelivery));


#if 0
    DBGLOG(SW4, INFO, ("IsQmmSup  %u\n",prStaRec->fgIsWmmSupported));
    DBGLOG(SW4, INFO, ("IsUapsdSup  %u\n",prStaRec->fgIsUapsdSupported));
    DBGLOG(SW4, INFO, ("AvailabaleDeliverPkts  %u\n",prStaRec->ucAvailableDeliverPkts));
    DBGLOG(SW4, INFO, ("BmpDeliverPktsAC  %u\n",prStaRec->u4BmpDeliverPktsAC));
    DBGLOG(SW4, INFO, ("BmpBufferAC  %u\n",prStaRec->u4BmpBufferAC));
    DBGLOG(SW4, INFO, ("BmpNonDeliverPktsAC  %u\n",prStaRec->u4BmpNonDeliverPktsAC));
#endif

    for(i=0;i<CFG_RX_MAX_BA_TID_NUM;i++) {
        if(prStaRec->aprRxReorderParamRefTbl[i]){
            DBGLOG(SW4, INFO,("RxReorder fgIsValid: %u\n",prStaRec->aprRxReorderParamRefTbl[i]->fgIsValid));
            DBGLOG(SW4, INFO,("RxReorder Tid: %u\n",prStaRec->aprRxReorderParamRefTbl[i]->ucTid));
            DBGLOG(SW4, INFO,("RxReorder rReOrderQue Len: %u\n", prStaRec->aprRxReorderParamRefTbl[i]->rReOrderQue.u4NumElem));
            DBGLOG(SW4, INFO,("RxReorder WinStart: %u\n", prStaRec->aprRxReorderParamRefTbl[i]->u2WinStart));
            DBGLOG(SW4, INFO,("RxReorder WinEnd: %u\n", prStaRec->aprRxReorderParamRefTbl[i]->u2WinEnd));
            DBGLOG(SW4, INFO,("RxReorder WinSize: %u\n", prStaRec->aprRxReorderParamRefTbl[i]->u2WinSize));
        }
    }

}


VOID dumpBss(P_ADAPTER_T prAdapter, P_BSS_INFO_T prBssInfo)
{

    DBGLOG(SW4, INFO, ("SSID %s\n",prBssInfo->aucSSID));
    DBGLOG(SW4, INFO, ("OWN " MACSTR"\n",MAC2STR(prBssInfo->aucOwnMacAddr)));
    DBGLOG(SW4, INFO, ("BSSID " MACSTR"\n",MAC2STR(prBssInfo->aucBSSID)));
    DBGLOG(SW4, INFO, ("ucNetTypeIndex %u\n",prBssInfo->ucNetTypeIndex));
    DBGLOG(SW4, INFO, ("eConnectionState %u\n",prBssInfo->eConnectionState));
    DBGLOG(SW4, INFO, ("eCurrentOPMode %u\n",prBssInfo->eCurrentOPMode));
    DBGLOG(SW4, INFO, ("fgIsQBSS %u\n",prBssInfo->fgIsQBSS));
    DBGLOG(SW4, INFO, ("fgIsShortPreambleAllowed %u\n",prBssInfo->fgIsShortPreambleAllowed));
    DBGLOG(SW4, INFO, ("fgUseShortPreamble %u\n",prBssInfo->fgUseShortPreamble));
    DBGLOG(SW4, INFO, ("fgUseShortSlotTime %u\n",prBssInfo->fgUseShortSlotTime));
    DBGLOG(SW4, INFO, ("ucNonHTBasicPhyType %x\n",prBssInfo->ucNonHTBasicPhyType));
    DBGLOG(SW4, INFO, ("u2OperationalRateSet %x\n",prBssInfo->u2OperationalRateSet));
    DBGLOG(SW4, INFO, ("u2BSSBasicRateSet %x\n",prBssInfo->u2BSSBasicRateSet));
    DBGLOG(SW4, INFO, ("ucPhyTypeSet %x\n",prBssInfo->ucPhyTypeSet));
    DBGLOG(SW4, INFO, ("rStaRecOfClientList %d\n",prBssInfo->rStaRecOfClientList.u4NumElem));
    DBGLOG(SW4, INFO, ("u2CapInfo %x\n",prBssInfo->u2CapInfo));
    DBGLOG(SW4, INFO, ("u2ATIMWindow %x\n",prBssInfo->u2ATIMWindow));
    DBGLOG(SW4, INFO, ("u2AssocId %x\n",prBssInfo->u2AssocId));
    DBGLOG(SW4, INFO, ("ucDTIMPeriod %x\n",prBssInfo->ucDTIMPeriod));
    DBGLOG(SW4, INFO, ("ucDTIMCount %x\n",prBssInfo->ucDTIMCount));
    DBGLOG(SW4, INFO, ("fgIsNetAbsent %x\n", prBssInfo->fgIsNetAbsent));
    DBGLOG(SW4, INFO, ("eBand %d\n", prBssInfo->eBand));
    DBGLOG(SW4, INFO, ("ucPrimaryChannel %d\n", prBssInfo->ucPrimaryChannel));
    DBGLOG(SW4, INFO, ("ucHtOpInfo1 %d\n", prBssInfo->ucHtOpInfo1));
    DBGLOG(SW4, INFO, ("ucHtOpInfo2 %d\n", prBssInfo->u2HtOpInfo2));
    DBGLOG(SW4, INFO, ("ucHtOpInfo3 %d\n", prBssInfo->u2HtOpInfo3));
    DBGLOG(SW4, INFO, ("fgErpProtectMode %d\n", prBssInfo->fgErpProtectMode));
    DBGLOG(SW4, INFO, ("eHtProtectMode %d\n", prBssInfo->eHtProtectMode));
    DBGLOG(SW4, INFO, ("eGfOperationMode %d\n", prBssInfo->eGfOperationMode));
    DBGLOG(SW4, INFO, ("eRifsOperationMode %d\n", prBssInfo->eRifsOperationMode));
    DBGLOG(SW4, INFO, ("fgObssErpProtectMode %d\n", prBssInfo->fgObssErpProtectMode));
    DBGLOG(SW4, INFO, ("eObssHtProtectMode %d\n", prBssInfo->eObssHtProtectMode));
    DBGLOG(SW4, INFO, ("eObssGfProtectMode %d\n", prBssInfo->eObssGfOperationMode));
    DBGLOG(SW4, INFO, ("fgObssRifsOperationMode %d\n", prBssInfo->fgObssRifsOperationMode));
    DBGLOG(SW4, INFO, ("fgAssoc40mBwAllowed %d\n", prBssInfo->fgAssoc40mBwAllowed));
    DBGLOG(SW4, INFO, ("fg40mBwAllowed %d\n", prBssInfo->fg40mBwAllowed));
    DBGLOG(SW4, INFO, ("eBssSCO %d\n", prBssInfo->eBssSCO));


}



VOID swCtrlCmdCategory0(P_ADAPTER_T prAdapter, UINT_8 ucCate, UINT_8 ucAction, UINT_8 ucOpt0,UINT_8 ucOpt1)
{
    UINT_8 ucIndex,ucRead;
    UINT_32 i;

    DEBUGFUNC("swCtrlCmdCategory0");

    SWCR_GET_RW_INDEX(ucAction,ucRead,ucIndex);

    i=0;

    if(ucIndex>=SWCTRL_CATA0_INDEX_NUM) return;

    if(ucRead == SWCR_WRITE) {
        switch(ucIndex) {
            case SWCTRL_DEBUG:
#if DBG
                aucDebugModule[ucOpt0] = (UINT_8)g_au4SwCr[1];
#endif
                break;
            case SWCTRL_WIFI_VAR:
               break;

#if QM_DEBUG_COUNTER
           case SWCTRL_DUMP_QM_DBG_CNT:
                for(i=0;i<QM_DBG_CNT_NUM;i++) {
                    prAdapter->rQM.au4QmDebugCounters[i] = 0;
                }
                break;
           case SWCTRL_QM_DBG_CNT:
                prAdapter->rQM.au4QmDebugCounters[ucOpt0] = g_au4SwCr[1];

               break;
#endif
#if CFG_RX_PKTS_DUMP
           case SWCTRL_RX_PKTS_DUMP:
           	   //DBGLOG(SW4, INFO,("SWCTRL_RX_PKTS_DUMP: mask %x\n", g_au4SwCr[1]));
           	   prAdapter->rRxCtrl.u4RxPktsDumpTypeMask = g_au4SwCr[1];
           	   break;
#endif
            default:
                break;
        }
    }
    else {
        switch(ucIndex) {
            case SWCTRL_DEBUG:
#if DBG
                g_au4SwCr[1] = aucDebugModule[ucOpt0] ;
#endif
                break;
            case SWCTRL_MAGIC:
                g_au4SwCr[1] = _SWCTRL_MAGIC ;
                DBGLOG(SW4, INFO,("BUILD TIME: %s %s\n", __DATE__, __TIME__));
                break;
            case SWCTRL_QM_INFO:
                    {
                        P_QUE_MGT_T prQM = &prAdapter->rQM;
                        switch(ucOpt0) {
                            case 0:
                               g_au4SwCr[1] = (QM_GET_TX_QUEUE_LEN(prAdapter, ucOpt1)) ;
                               g_au4SwCr[2] = prQM->au4MinReservedTcResource[ucOpt1] ;
                               g_au4SwCr[3] = prQM->au4CurrentTcResource[ucOpt1];
                               g_au4SwCr[4] = prQM->au4GuaranteedTcResource[ucOpt1];
                                break;

                            case 1:
                                g_au4SwCr[1] = prQM->au4ForwardCount[ucOpt1];
                                g_au4SwCr[2] = prQM->au4HeadStaRecIndex[ucOpt1];
                                break;

                            case 2:
                                g_au4SwCr[1] = prQM->arTxQueue[ucOpt1].u4NumElem; /* only one */


                                break;
                        }

                    }
            case SWCTRL_TX_CTRL_INFO:
                    {
                        P_TX_CTRL_T prTxCtrl;
                        prTxCtrl = &prAdapter->rTxCtrl;
                        switch(ucOpt0) {
                               case 0:
                                    g_au4SwCr[1] =  prAdapter->rTxCtrl.rTc.aucFreeBufferCount[ucOpt1];
                                    g_au4SwCr[2] =  prAdapter->rTxCtrl.rTc.aucMaxNumOfBuffer[ucOpt1];
                                    break;
                        }

                    }
                    break;
           case SWCTRL_DUMP_QUEUE:
                    dumpQueue(prAdapter);

                    break;
#if QM_DEBUG_COUNTER
           case SWCTRL_DUMP_QM_DBG_CNT:
                    for(i=0;i<QM_DBG_CNT_NUM;i++) {
                        DBGLOG(SW4, INFO,("QM:DBG %u %u\n",i , prAdapter->rQM.au4QmDebugCounters[i]));
                    }
                    break;

           case SWCTRL_QM_DBG_CNT:
                    g_au4SwCr[1] = prAdapter->rQM.au4QmDebugCounters[ucOpt0];
                    break;
#endif
            case SWCTRL_DUMP_BSS:
                    {
                        dumpBss(prAdapter, &(prAdapter->rWifiVar.arBssInfo[ucOpt0])) ;
                    }
                    break;

            default:
                    break;
        }

    }
}


VOID swCtrlCmdCategory1(P_ADAPTER_T prAdapter, UINT_8 ucCate, UINT_8 ucAction, UINT_8 ucOpt0,UINT_8 ucOpt1)
{
    UINT_8 ucIndex,ucRead;
    UINT_8 ucWTEntry;
    P_STA_RECORD_T prStaRec;

    DEBUGFUNC("swCtrlCmdCategory1");

    SWCR_GET_RW_INDEX(ucAction,ucRead,ucIndex);

    if(ucOpt0>=CFG_STA_REC_NUM) return;

    //prStaRec = cnmGetStaRecByIndex (prAdapter, ucOpt0);
    prStaRec = &prAdapter->arStaRec[ucOpt0];
    ucWTEntry =  prStaRec->ucWTEntry;
    if(ucRead == SWCR_WRITE) {
    }
    else {
        /* Read */
        switch(ucIndex) {
            case SWCTRL_STA_QUE_INFO:
                {
                    g_au4SwCr[1] = prStaRec->arTxQueue[ucOpt1].u4NumElem;
                }
                break;
            case SWCTRL_STA_INFO:
                switch(ucOpt1) {
                    case 0:
                        g_au4SwCr[1] = prStaRec->fgIsInPS;
                        break;
                }

                break;

             case SWCTRL_DUMP_STA:
                 {
                     dumpSTA(prAdapter, prStaRec);
                 }
                 break;

             default:

                 break;
        }
    }


}

#if TEST_PS

VOID
testPsSendQoSNullFrame (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec,
    IN UINT_8           ucUP,
    IN UINT_8           ucNetTypeIndex,
    IN BOOLEAN          fgBMC,
    IN BOOLEAN          fgIsBurstEnd,
    IN BOOLEAN          ucPacketType,
    IN BOOLEAN          ucPsSessionID,
    IN BOOLEAN          fgSetEOSP
    )
{
    P_MSDU_INFO_T prMsduInfo;
    UINT_16 u2EstimatedFrameLen;
    P_WLAN_MAC_HEADER_QOS_T prQoSNullFrame;


    DEBUGFUNC("testPsSendQoSNullFrame");
    DBGLOG(SW4, LOUD, ("\n"));

    //4 <1> Allocate a PKT_INFO_T for Null Frame
    /* Init with MGMT Header Length */
    u2EstimatedFrameLen = MAC_TX_RESERVED_FIELD + \
                          WLAN_MAC_HEADER_QOS_LEN;

    /* Allocate a MSDU_INFO_T */
    if ( (prMsduInfo = cnmMgtPktAlloc(prAdapter, u2EstimatedFrameLen)) == NULL) {
        DBGLOG(SW4, WARN, ("No PKT_INFO_T for sending Null Frame.\n"));
        return ;
    }

    //4 <2> Compose Null frame in MSDU_INfO_T.
    bssComposeQoSNullFrame(prAdapter,
            (PUINT_8)((UINT_32)(prMsduInfo->prPacket) + MAC_TX_RESERVED_FIELD),
            prStaRec,
            ucUP,
            fgSetEOSP);


    prMsduInfo->eSrc = TX_PACKET_MGMT;
    //prMsduInfo->ucPacketType = HIF_TX_PACKET_TYPE_DATA;
    prMsduInfo->ucPacketType = ucPacketType;
    prMsduInfo->ucStaRecIndex = prStaRec->ucIndex;
    prMsduInfo->ucNetworkType = ucNetTypeIndex;
    prMsduInfo->ucMacHeaderLength = WLAN_MAC_HEADER_QOS_LEN;
    prMsduInfo->fgIs802_1x = FALSE;
    prMsduInfo->fgIs802_11 = TRUE;
    prMsduInfo->u2FrameLength = WLAN_MAC_HEADER_QOS_LEN;
    prMsduInfo->ucTxSeqNum = nicIncreaseTxSeqNum(prAdapter);
    prMsduInfo->pfTxDoneHandler = NULL;
    prMsduInfo->fgIsBasicRate = TRUE;
    prMsduInfo->fgIsBurstEnd = fgIsBurstEnd;
    prMsduInfo->ucUserPriority = ucUP;
    prMsduInfo->ucPsSessionID = ucPsSessionID  /* 0~7 Test 7 means NOACK*/;

    prQoSNullFrame = (P_WLAN_MAC_HEADER_QOS_T)(  (PUINT_8)((UINT_32)(prMsduInfo->prPacket) + MAC_TX_RESERVED_FIELD)   );

    if(fgBMC) {
        prQoSNullFrame->aucAddr1[0] = 0xfd;
    }
    else {
        prQoSNullFrame->aucAddr1[5] = 0xdd;
    }

    //4 <4> Inform TXM  to send this Null frame.
    nicTxEnqueueMsdu(prAdapter, prMsduInfo);

}



VOID
testPsSetupBss(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucNetworkTypeIndex
    )
{
    P_BSS_INFO_T prBssInfo;
    UINT_8 _aucZeroMacAddr[] = NULL_MAC_ADDR;

    DEBUGFUNC("testPsSetupBss()");
    DBGLOG(SW4, INFO, ("index %d\n", ucNetworkTypeIndex));

    prBssInfo = &(prAdapter->rWifiVar.arBssInfo[ucNetworkTypeIndex]);

    //4 <1.2> Initiate PWR STATE
    //SET_NET_PWR_STATE_IDLE(prAdapter, ucNetworkTypeIndex);


    //4 <2> Initiate BSS_INFO_T - common part
    BSS_INFO_INIT(prAdapter, ucNetworkTypeIndex);

    prBssInfo->eConnectionState = PARAM_MEDIA_STATE_DISCONNECTED;
    prBssInfo->eConnectionStateIndicated = PARAM_MEDIA_STATE_DISCONNECTED;
    prBssInfo->eCurrentOPMode = OP_MODE_ACCESS_POINT;
    prBssInfo->fgIsNetActive = TRUE;
    prBssInfo->ucNetTypeIndex = (ucNetworkTypeIndex);
    prBssInfo->ucReasonOfDisconnect = DISCONNECT_REASON_CODE_RESERVED;

    prBssInfo->ucPhyTypeSet = PHY_TYPE_SET_802_11BG; /* Depend on eBand */
    prBssInfo->ucConfigAdHocAPMode = AP_MODE_MIXED_11BG; /* Depend on eCurrentOPMode and ucPhyTypeSet */
    prBssInfo->u2BSSBasicRateSet = RATE_SET_ERP;
    prBssInfo->u2OperationalRateSet = RATE_SET_OFDM;
    prBssInfo->fgErpProtectMode = FALSE;
    prBssInfo->fgIsQBSS = TRUE;

    //4 <1.5> Setup MIB for current BSS
    prBssInfo->u2BeaconInterval = 100;
    prBssInfo->ucDTIMPeriod = DOT11_DTIM_PERIOD_DEFAULT;
    prBssInfo->u2ATIMWindow = 0;

    prBssInfo->ucBeaconTimeoutCount = 0;


    bssInitForAP (prAdapter,prBssInfo, TRUE);

    COPY_MAC_ADDR(prBssInfo->aucBSSID, _aucZeroMacAddr);
    LINK_INITIALIZE(&prBssInfo->rStaRecOfClientList);
    prBssInfo->fgIsBeaconActivated = TRUE;
    prBssInfo->ucHwDefaultFixedRateCode = RATE_CCK_1M_LONG;


    COPY_MAC_ADDR(prBssInfo->aucOwnMacAddr, prAdapter->rWifiVar.aucMacAddress);

    //4 <3> Initiate BSS_INFO_T - private part
    /* TODO */
    prBssInfo->eBand = BAND_2G4;
    prBssInfo->ucPrimaryChannel = 1;
    prBssInfo->prStaRecOfAP = (P_STA_RECORD_T)NULL;


    //prBssInfo->fgErpProtectMode =  eErpProectMode;
    //prBssInfo->eHtProtectMode = eHtProtectMode;
    //prBssInfo->eGfOperationMode = eGfOperationMode;


    //4 <4> Allocate MSDU_INFO_T for Beacon
    prBssInfo->prBeacon = cnmMgtPktAlloc(prAdapter,
            OFFSET_OF(WLAN_BEACON_FRAME_T, aucInfoElem[0]) + MAX_IE_LENGTH);

    if (prBssInfo->prBeacon) {
        prBssInfo->prBeacon->eSrc = TX_PACKET_MGMT;
        prBssInfo->prBeacon->ucNetworkType = ucNetworkTypeIndex;
    }
    else {
        DBGLOG(SW4, INFO, ("prBeacon allocation fail\n"));
    }

#if 0
    prBssInfo->rPmProfSetupInfo.ucBmpDeliveryAC = PM_UAPSD_ALL;
    prBssInfo->rPmProfSetupInfo.ucBmpTriggerAC = PM_UAPSD_ALL;
    prBssInfo->rPmProfSetupInfo.ucUapsdSp = WMM_MAX_SP_LENGTH_2;
#else
    prBssInfo->rPmProfSetupInfo.ucBmpDeliveryAC = (UINT_8)prAdapter->u4UapsdAcBmp;
    prBssInfo->rPmProfSetupInfo.ucBmpTriggerAC =(UINT_8) prAdapter->u4UapsdAcBmp;
    prBssInfo->rPmProfSetupInfo.ucUapsdSp = (UINT_8)prAdapter->u4MaxSpLen;
#endif

#if 0
    for(eAci = 0; eAci < WMM_AC_INDEX_NUM; eAci++){

        prBssInfo->arACQueParms[eAci].fgIsACMSet = FALSE;
        prBssInfo->arACQueParms[eAci].u2Aifsn = (UINT_16) eAci;
        prBssInfo->arACQueParms[eAci].u2CWmin = 7;
        prBssInfo->arACQueParms[eAci].u2CWmax = 31;
        prBssInfo->arACQueParms[eAci].u2TxopLimit = eAci+1;
        DBGLOG(SW4, INFO, ("MQM: eAci = %d, ACM = %d, Aifsn = %d, CWmin = %d, CWmax = %d, TxopLimit = %d\n",
                   eAci,prBssInfo->arACQueParms[eAci].fgIsACMSet ,
                   prBssInfo->arACQueParms[eAci].u2Aifsn,
                   prBssInfo->arACQueParms[eAci].u2CWmin,
                   prBssInfo->arACQueParms[eAci].u2CWmax,
                   prBssInfo->arACQueParms[eAci].u2TxopLimit));

    }
#endif


    DBGLOG(SW4, INFO, ("[2] ucBmpDeliveryAC:0x%x, ucBmpTriggerAC:0x%x, ucUapsdSp:0x%x",
            prBssInfo->rPmProfSetupInfo.ucBmpDeliveryAC,
            prBssInfo->rPmProfSetupInfo.ucBmpTriggerAC,
            prBssInfo->rPmProfSetupInfo.ucUapsdSp));

    return;
}




VOID testPsCmdCategory0(P_ADAPTER_T prAdapter, UINT_8 ucCate, UINT_8 ucAction, UINT_8 ucOpt0,UINT_8 ucOpt1)
{
    UINT_8 ucIndex,ucRead;
    P_STA_RECORD_T prStaRec;

    DEBUGFUNC("testPsCmdCategory0");
    SWCR_GET_RW_INDEX(ucAction,ucRead,ucIndex);

    DBGLOG(SW4, LOUD, ("Read %u Index %u\n",ucRead,ucIndex));

    prStaRec = cnmGetStaRecByIndex (prAdapter, 0);

    if(ucIndex>=TEST_PS_CATA0_INDEX_NUM) return;

    if(ucRead == SWCR_WRITE) {
        switch(ucIndex) {
            case TEST_PS_SETUP_BSS:
                testPsSetupBss(prAdapter, ucOpt0) ;
                break;

            case TEST_PS_ENABLE_BEACON:
               break;

            case TEST_PS_TRIGGER_BMC:
                //txmForwardQueuedBmcPkts (ucOpt0);
                break;
            case TEST_PS_SEND_NULL:
                {

                    testPsSendQoSNullFrame (prAdapter,prStaRec,
                            (UINT_8)(g_au4SwCr[1] & 0xFF), /* UP */
                            ucOpt0,
                            (BOOLEAN)((g_au4SwCr[1] >>8)& 0xFF), /* BMC*/
                            (BOOLEAN)((g_au4SwCr[1] >>16)& 0xFF), /* BurstEnd*/
                            (BOOLEAN)((g_au4SwCr[1] >>24)& 0xFF), /* Packet type*/
                            (UINT_8)((g_au4SwCr[2] )& 0xFF), /* PS sesson ID 7: NOACK */
                            FALSE                                  /* EOSP */
                        );
                }
                    break;
            case TEST_PS_BUFFER_BMC:
                //g_aprBssInfo[ucOpt0]->fgApToBufferBMC = (g_au4SwCr[1] & 0xFF);
                break;
            case TEST_PS_UPDATE_BEACON:
                bssUpdateBeaconContent(prAdapter, ucOpt0 /*networktype*/ );
                break;

           default:
                break;
        }
    }
    else {
        switch(ucIndex) {

            case TEST_PS_MAGIC:
                g_au4SwCr[1] = 0x88660011 ;
                break;

        }
    }
}

#endif //TEST_PS

#if TEST_PS

VOID testPsCmdCategory1(P_ADAPTER_T prAdapter, UINT_8 ucCate, UINT_8 ucAction, UINT_8 ucOpt0,UINT_8 ucOpt1)
{
    UINT_8 ucIndex,ucRead;
    UINT_8 ucWTEntry;
    P_STA_RECORD_T prStaRec;

    DEBUGFUNC("testPsCmdCategory1");

    SWCR_GET_RW_INDEX(ucAction,ucRead,ucIndex);

    if(ucOpt0>=CFG_STA_REC_NUM) return;

    prStaRec = cnmGetStaRecByIndex (prAdapter, ucOpt0);
    ucWTEntry =  prStaRec->ucWTEntry;
    if(ucRead == SWCR_WRITE) {

        switch(ucIndex) {
            case TEST_PS_STA_PS:
                prStaRec->fgIsInPS = (BOOLEAN) (g_au4SwCr[1] & 0x1);
                prStaRec->fgIsQoS = (BOOLEAN) (g_au4SwCr[1] >>8 & 0xFF);
                prStaRec->fgIsUapsdSupported = (BOOLEAN) (g_au4SwCr[1] >>16 & 0xFF);
                prStaRec->ucBmpDeliveryAC = (BOOLEAN) (g_au4SwCr[1] >>24 & 0xFF);
                break;

        }

    }
    else {
        /* Read */
        switch(ucIndex) {
            default:
                break;
        }
    }


}

#endif //TEST_PS



VOID swCtrlSwCr(P_ADAPTER_T prAdapter, UINT_8 ucRead, UINT_16 u2Addr, UINT_32 *pu4Data)
{
    /* According other register STAIDX */
    UINT_8      ucOffset;
    ucOffset = (u2Addr>>2) & 0x3F;

    if(ucOffset>= SWCR_CR_NUM) return;

    if(ucRead==SWCR_WRITE) {
        g_au4SwCr[ucOffset] = *pu4Data;
        if(ucOffset==0x0) {
            /* Commmand   [31:24]: Category */
            /* Commmand   [23:23]: 1(W) 0(R) */
            /* Commmand   [22:16]: Index */
            /* Commmand   [15:08]: Option0  */
            /* Commmand   [07:00]: Option1   */
            UINT_8 ucCate;
            UINT_32 u4Cmd;
            u4Cmd = g_au4SwCr[0];
            ucCate = (UINT_8)(u4Cmd >> 24) ;
            if(ucCate < sizeof(g_arSwCtrlCmd)/sizeof(g_arSwCtrlCmd[0])) {
                if(g_arSwCtrlCmd[ucCate]!=NULL) {
                    g_arSwCtrlCmd[ucCate](prAdapter, ucCate, (UINT_8)(u4Cmd>>16 & 0xFF),(UINT_8)((u4Cmd>>8) & 0xFF), (UINT_8)(u4Cmd&0xFF));
                }
            }
        }
    }
    else {
        *pu4Data = g_au4SwCr[ucOffset];
    }
}

VOID swCrReadWriteCmd(P_ADAPTER_T prAdapter, UINT_8 ucRead, UINT_16 u2Addr, UINT_32 *pu4Data)
{
    UINT_8 ucMod;

    ucMod =  u2Addr >>8;
    /* Address [15:8] MOD ID */
    /* Address [7:0] OFFSET */

    DEBUGFUNC("swCrReadWriteCmd");
    DBGLOG(SW4, INFO, ("%u addr 0x%x data 0x%x\n",ucRead,u2Addr,*pu4Data));

    if(ucMod < (sizeof(g_arSwCrModHandle)/sizeof(g_arSwCrModHandle[0])) ) {

        if(g_arSwCrModHandle[ucMod]!=NULL) {
         g_arSwCrModHandle[ucMod](prAdapter, ucRead, u2Addr, pu4Data);
        }
   } /* ucMod */
}

/* Debug Support */
VOID swCrFrameCheckEnable(P_ADAPTER_T  prAdapter, UINT_32 u4DumpType)
{
	  g_u4SwcrDebugFrameDumpType = u4DumpType;
	  prAdapter->rRxCtrl.u4RxPktsDumpTypeMask = u4DumpType;
}

VOID swCrDebugInit(P_ADAPTER_T  prAdapter)
{
	  // frame dump
    if (g_u4SwcrDebugFrameDumpType) {
    	  swCrFrameCheckEnable(prAdapter, g_u4SwcrDebugFrameDumpType);
    }

    // debug counter
    g_fgSwcrDebugTimer = FALSE;

    cnmTimerInitTimer(prAdapter,
                      &g_rSwcrDebugTimer,
                      (PFN_MGMT_TIMEOUT_FUNC)swCrDebugCheckTimeout,
                      (UINT_32) NULL);

    if (g_u4SwcrDebugCheckTimeout) {
    	  swCrDebugCheckEnable(prAdapter, TRUE, g_ucSwcrDebugCheckType, g_u4SwcrDebugCheckTimeout);
    }
}

VOID swCrDebugUninit(P_ADAPTER_T  prAdapter)
{
	  cnmTimerStopTimer(prAdapter, &g_rSwcrDebugTimer);

	  g_fgSwcrDebugTimer = FALSE;
}

VOID swCrDebugCheckEnable(P_ADAPTER_T  prAdapter, BOOLEAN fgIsEnable, UINT_8 ucType, UINT_32 u4Timeout)
{
	  if (fgIsEnable) {
	  	  g_ucSwcrDebugCheckType = ucType;
	  	  g_u4SwcrDebugCheckTimeout = u4Timeout;
	  	  if (g_fgSwcrDebugTimer == FALSE) {
	  	      swCrDebugCheckTimeout(prAdapter, 0);
	  	  }
	  }
	  else {
	  	  cnmTimerStopTimer(prAdapter, &g_rSwcrDebugTimer);
	  	  g_u4SwcrDebugCheckTimeout = 0;
	  }

	  g_fgSwcrDebugTimer = fgIsEnable;
}

VOID swCrDebugCheck(P_ADAPTER_T  prAdapter, P_CMD_SW_DBG_CTRL_T prCmdSwCtrl)
{
    P_RX_CTRL_T prRxCtrl;
    P_TX_CTRL_T prTxCtrl;

    ASSERT(prAdapter);

    prTxCtrl = &prAdapter->rTxCtrl;
    prRxCtrl = &prAdapter->rRxCtrl;

    // dump counters
    if (prCmdSwCtrl) {
    	  if (prCmdSwCtrl->u4Data == SWCR_DBG_TYPE_ALL) {

            // TX Counter from fw
            DBGLOG(SW4, INFO,  ("TX0\n" \
                               "%08x %08x %08x %08x\n" \
                               "%08x %08x %08x %08x\n",
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_TX_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_TX_BCN_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_TX_FAILED_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_TX_RETRY_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_TX_AGING_TIMEOUT_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_TX_PS_OVERFLOW_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_TX_MGNT_DROP_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_TX_ERROR_CNT]));
#if 1
            // TX Counter from drv
            DBGLOG(SW4, INFO,  ("TX1\n" \
                               "%08x %08x %08x %08x\n",
                               (UINT_32)TX_GET_CNT(prTxCtrl, TX_INACTIVE_BSS_DROP),
                               (UINT_32)TX_GET_CNT(prTxCtrl, TX_INACTIVE_STA_DROP),
                               (UINT_32)TX_GET_CNT(prTxCtrl, TX_FORWARD_OVERFLOW_DROP),
                               (UINT_32)TX_GET_CNT(prTxCtrl, TX_AP_BORADCAST_DROP)));
#endif

            // RX Counter
            DBGLOG(SW4, INFO,  ("RX0\n" \
                               "%08x %08x %08x %08x\n" \
                               "%08x %08x %08x %08x\n" \
                               "%08x %08x %08x %08x\n" \
                               "%08x %08x %08x %08x\n",
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_DROP_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_DUP_DROP_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_TYPE_ERROR_DROP_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_CLASS_ERROR_DROP_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_AMPDU_ERROR_DROP_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_STATUS_ERROR_DROP_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_FORMAT_ERROR_DROP_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_ICV_ERROR_DROP_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_KEY_ERROR_DROP_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_TKIP_ERROR_DROP_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_MIC_ERROR_DROP_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_BIP_ERROR_DROP_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_FCSERR_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_FIFOFULL_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_RX_PFDROP_CNT]));

            DBGLOG(SW4, INFO,  ("RX1\n" \
                               "%08x %08x %08x %08x\n" \
                               "%08x %08x %08x %08x\n",
                               (UINT_32)RX_GET_CNT(prRxCtrl, RX_MPDU_TOTAL_COUNT),
                               (UINT_32)RX_GET_CNT(prRxCtrl, RX_DATA_INDICATION_COUNT),
                               (UINT_32)RX_GET_CNT(prRxCtrl, RX_DATA_RETURNED_COUNT),
                               (UINT_32)RX_GET_CNT(prRxCtrl, RX_DATA_RETAINED_COUNT),
                               (UINT_32)RX_GET_CNT(prRxCtrl, RX_DROP_TOTAL_COUNT),
                               (UINT_32)RX_GET_CNT(prRxCtrl, RX_TYPE_ERR_DROP_COUNT),
                               (UINT_32)RX_GET_CNT(prRxCtrl, RX_CLASS_ERR_DROP_COUNT),
                               (UINT_32)RX_GET_CNT(prRxCtrl, RX_DST_NULL_DROP_COUNT)));

            DBGLOG(SW4, INFO,  ("PWR\n" \
                               "%08x %08x %08x %08x\n" \
                               "%08x %08x %08x %08x\n",
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_PWR_PS_POLL_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_PWR_TRIGGER_NULL_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_PWR_BCN_IND_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_PWR_BCN_TIMEOUT_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_PWR_PM_STATE0],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_PWR_PM_STATE1],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_PWR_CUR_PS_PROF0],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_PWR_CUR_PS_PROF1]));

            DBGLOG(SW4, INFO,  ("ARM\n" \
                               "%08x %08x %08x %08x\n" \
                               "%08x %08x\n",
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_AR_STA0_RATE],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_AR_STA0_BWGI],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_AR_STA0_RX_RATE_RCPI],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_ROAMING_ENABLE],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_ROAMING_ROAM_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_ROAMING_INT_CNT]));

            DBGLOG(SW4, INFO,  ("BB\n" \
                               "%08x %08x %08x %08x\n" \
                               "%08x %08x %08x %08x\n",
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_BB_RX_MDRDY_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_BB_RX_FCSERR_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_BB_CCK_PD_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_BB_OFDM_PD_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_BB_CCK_SFDERR_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_BB_CCK_SIGERR_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_BB_OFDM_TAGERR_CNT],
                               prCmdSwCtrl->u4DebugCnt[SWCR_DBG_ALL_BB_OFDM_SIGERR_CNT]));

        }
    }

    // start the next check
	  if (g_u4SwcrDebugCheckTimeout) {
	  	  cnmTimerStartTimer(prAdapter, &g_rSwcrDebugTimer, g_u4SwcrDebugCheckTimeout * MSEC_PER_SEC);
	  }
}

VOID swCrDebugCheckTimeout(
    IN P_ADAPTER_T prAdapter,
    UINT_32 u4Param)
{
	  CMD_SW_DBG_CTRL_T rCmdSwCtrl;
	  WLAN_STATUS rStatus;

    rCmdSwCtrl.u4Id = (0xb000<<16) + g_ucSwcrDebugCheckType;
    rCmdSwCtrl.u4Data = 0;
	  rStatus = wlanSendSetQueryCmd (
                prAdapter,                  /* prAdapter */
                CMD_ID_SW_DBG_CTRL,       /* ucCID */
                FALSE,                      /* fgSetQuery */
                TRUE,                       /* fgNeedResp */
                FALSE,                      /* fgIsOid */
                swCrDebugQuery,             /* pfCmdDoneHandler */
                swCrDebugQueryTimeout,      /* pfCmdTimeoutHandler */
                sizeof(CMD_SW_DBG_CTRL_T),  /* u4SetQueryInfoLen */
                (PUINT_8)&rCmdSwCtrl,       /* pucInfoBuffer */
                NULL,                       /* pvSetQueryBuffer */
                0                           /* u4SetQueryBufferLen */
                );

    ASSERT(rStatus == WLAN_STATUS_PENDING);

    return;
}

VOID swCrDebugQuery(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    )
{
    ASSERT(prAdapter);

    swCrDebugCheck(prAdapter, (P_CMD_SW_DBG_CTRL_T)(pucEventBuf));
}

VOID swCrDebugQueryTimeout(
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo)
{
	  ASSERT(prAdapter);

    swCrDebugCheck(prAdapter, NULL);
}

#endif /* CFG_SUPPORT_SWCR */


