/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_2/mgmt/privacy.c#1 $
*/

/*! \file   "privacy.c"
    \brief  This file including the protocol layer privacy function.

    This file provided the macros and functions library support for the
    protocol layer security setting from rsn.c and nic_privacy.c

*/

/*******************************************************************************
* Copyright (c) 2007 MediaTek Inc.
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
** $Log: privacy.c $
 *
 * 11 10 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * change the debug module level.
 *
 * 10 20 2011 terry.wu
 * NULL
 * Fix Hotspot deauth send failed.
 *
 * 10 19 2011 yuche.tsai
 * [WCXRP00001045] [WiFi Direct][Driver] Check 2.1 branch.
 * Branch 2.1
 * Davinci Maintrunk Label: MT6620_WIFI_DRIVER_FW_TRUNK_MT6620E5_111019_0926.
 *
 * 06 28 2011 tsaiyuan.hsu
 * [WCXRP00000819] [MT6620 Wi-Fi][Driver] check if staRec is NULL or not in secCheckClassError
 * check if staRec is NULL or not in secCheckClassError.
 *
 * 06 09 2011 tsaiyuan.hsu
 * [WCXRP00000760] [MT5931 Wi-Fi][FW] Refine rxmHandleMacRxDone to reduce code size
 * move send_auth at rxmHandleMacRxDone in firmware to driver to reduce code size.
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 01 25 2011 yuche.tsai
 * [WCXRP00000388] [Volunteer Patch][MT6620][Driver/Fw] change Station Type in station record.
 * Change Station Type in Station Record, Modify MACRO definition for getting station type & network type index & Role.
 *
 * 11 04 2010 wh.su
 * [WCXRP00000164] [MT6620 Wi-Fi][Driver] Support the p2p random SSID
 * adding the p2p random ssid support.
 *
 * 10 04 2010 cp.wu
 * [WCXRP00000077] [MT6620 Wi-Fi][Driver][FW] Eliminate use of ENUM_NETWORK_TYPE_T and replaced by ENUM_NETWORK_TYPE_INDEX_T only
 * remove ENUM_NETWORK_TYPE_T definitions
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 09 01 2010 wh.su
 * NULL
 * adding the wapi support for integration test.
 *
 * 07 24 2010 wh.su
 *
 * .support the Wi-Fi RSN
 *
 * 07 20 2010 wh.su
 *
 * adding the wapi code.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 21 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * modify some code for concurrent network.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * enable RX management frame handling.
 *
 * 06 19 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * adding the compiling flag for migration.
 *
 * 06 19 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * consdier the concurrent network setting.
 *
 * 06 18 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * migration the security related function from firmware.
 *
 * 05 28 2010 wh.su
 * [BORA00000626][MT6620] Refine the remove key flow for WHQL testing
 * fixed the ad-hoc wpa-none send non-encrypted frame issue.
 *
 * 05 24 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Refine authSendAuthFrame() for NULL STA_RECORD_T case and minimum deauth interval.
 *
 * 04 29 2010 wh.su
 * [BORA00000637][MT6620 Wi-Fi] [Bug] WPA2 pre-authentication timer not correctly initialize
 * adjsut the pre-authentication code.
 *
 * 04 22 2010 wh.su
 * [BORA00000626][MT6620] Refine the remove key flow for WHQL testing
 * fixed the wpi same key id rx issue and fixed the remove wep key issue.
 *
 * 04 19 2010 kevin.huang
 * [BORA00000714][WIFISYS][New Feature]Beacon Timeout Support
 * Add Send Deauth for Class 3 Error and Leave Network Support
 *
 * 04 15 2010 wh.su
 * [BORA00000680][MT6620] Support the statistic for Microsoft os query
 * remove the assert code for allow ad-hoc pkt.
 *
 * 04 13 2010 wh.su
 * [BORA00000680][MT6620] Support the statistic for Microsoft os query
 * fixed the Klocwork error and refine the class error message.
 *
 * 03 04 2010 wh.su
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Code refine, and remove non-used code.
 *
 * 03 03 2010 wh.su
 * [BORA00000637][MT6620 Wi-Fi] [Bug] WPA2 pre-authentication timer not correctly initialize
 * move the AIS specific variable for security to AIS specific structure.
 *
 * 03 03 2010 wh.su
 * [BORA00000637][MT6620 Wi-Fi] [Bug] WPA2 pre-authentication timer not correctly initialize
 * Fixed the pre-authentication timer not correctly init issue, and modify the security related callback function prototype.
 *
 * 03 01 2010 wh.su
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Refine the variable and parameter for security.
 *
 * 02 26 2010 wh.su
 * [BORA00000626][MT6620] Refine the remove key flow for WHQL testing
 * change the waning message shown level, and clear the global transmit flag for CMD INFRASTRUCTURE.
 *
 * 02 25 2010 wh.su
 * [BORA00000626][MT6620] Refine the remove key flow for WHQL testing
 * For support the WHQL test, do the remove key code refine.
 *
 * 01 27 2010 wh.su
 * [BORA00000476][Wi-Fi][firmware] Add the security module initialize code
 * add and fixed some security function.
 *
 * 12 25 2009 tehuang.liu
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Integrated modifications for 1st connection (mainly on FW modules MQM, TXM, and RXM)
 *  *  *  *  *  *  *  *  * MQM: BA handling
 *  *  *  *  *  *  *  *  * TXM: Macros updates
 *  *  *  *  *  *  *  *  * RXM: Macros/Duplicate Removal updates
 *
 * 12 18 2009 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * .
 *
 * Dec 11 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * modify the cmd with result return
 *
 * Dec 11 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * fixed the value not initialize issue
 *
 * Dec 10 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * change the cmd return type
 *
 * Dec 8 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adding the function to update the auth mode and encryption status for cmd build connection
 *
 * Dec 7 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adding some code for wapi mode
 *
 * Dec 7 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adding the call to check the 4th and eapol error report frame
 *
 * Dec 7 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * rename the function name
 *
 * Dec 4 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adding the code for parsing the EAPoL frame, and do some code refine
 *
 * Dec 3 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adding the class error check
 *
 * Dec 3 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adding the cmd_802_11_pmkid code
 *
 * Dec 1 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * doing some function rename, and adding the code for cmd CMD_ADD_REMOVE_KEY
 *
 * Nov 23 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adding the clear pmkid function
 *
 * Nov 23 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Fix eStaType check for AIS
 *
 * Nov 19 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adding the ap selection related code
 *
 * Nov 18 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
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

#if CFG_PRIVACY_MIGRATION

/*******************************************************************************
*                              C O N S T A N T S
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
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to initialize the privacy-related
*        parameters.
*
* \param[in] prAdapter Pointer to the Adapter structure
* \param[in] ucNetTypeIdx  Pointer to netowrk type index
*
* \retval NONE
*/
/*----------------------------------------------------------------------------*/
VOID
secInit (
    IN P_ADAPTER_T          prAdapter,
    IN UINT_8               ucNetTypeIdx
    )
{
    UINT_8                  i;
    P_CONNECTION_SETTINGS_T prConnSettings;
    P_BSS_INFO_T            prBssInfo;
    P_AIS_SPECIFIC_BSS_INFO_T prAisSpecBssInfo;

    DEBUGFUNC("secInit");

    ASSERT(prAdapter);

    prConnSettings = &prAdapter->rWifiVar.rConnSettings;
    prBssInfo = &prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX];
    prAisSpecBssInfo = &prAdapter->rWifiVar.rAisSpecificBssInfo;

    prBssInfo->u4RsnSelectedGroupCipher = 0;
    prBssInfo->u4RsnSelectedPairwiseCipher = 0;
    prBssInfo->u4RsnSelectedAKMSuite = 0;

#if CFG_ENABLE_WIFI_DIRECT
    prBssInfo = &prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX];

    prBssInfo->u4RsnSelectedGroupCipher = RSN_CIPHER_SUITE_CCMP;
    prBssInfo->u4RsnSelectedPairwiseCipher = RSN_CIPHER_SUITE_CCMP;
    prBssInfo->u4RsnSelectedAKMSuite = RSN_AKM_SUITE_PSK;
#endif

#if CFG_ENABLE_BT_OVER_WIFI
    prBssInfo = &prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_BOW_INDEX];

    prBssInfo->u4RsnSelectedGroupCipher = RSN_CIPHER_SUITE_CCMP;
    prBssInfo->u4RsnSelectedPairwiseCipher = RSN_CIPHER_SUITE_CCMP;
    prBssInfo->u4RsnSelectedAKMSuite = RSN_AKM_SUITE_PSK;
#endif

    prAdapter->rMib.dot11RSNAConfigPairwiseCiphersTable[0].dot11RSNAConfigPairwiseCipher =
            WPA_CIPHER_SUITE_WEP40;
    prAdapter->rMib.dot11RSNAConfigPairwiseCiphersTable[1].dot11RSNAConfigPairwiseCipher =
            WPA_CIPHER_SUITE_TKIP;
    prAdapter->rMib.dot11RSNAConfigPairwiseCiphersTable[2].dot11RSNAConfigPairwiseCipher =
            WPA_CIPHER_SUITE_CCMP;
    prAdapter->rMib.dot11RSNAConfigPairwiseCiphersTable[3].dot11RSNAConfigPairwiseCipher =
            WPA_CIPHER_SUITE_WEP104;

    prAdapter->rMib.dot11RSNAConfigPairwiseCiphersTable[4].dot11RSNAConfigPairwiseCipher =
            RSN_CIPHER_SUITE_WEP40;
    prAdapter->rMib.dot11RSNAConfigPairwiseCiphersTable[5].dot11RSNAConfigPairwiseCipher =
            RSN_CIPHER_SUITE_TKIP;
    prAdapter->rMib.dot11RSNAConfigPairwiseCiphersTable[6].dot11RSNAConfigPairwiseCipher =
            RSN_CIPHER_SUITE_CCMP;
    prAdapter->rMib.dot11RSNAConfigPairwiseCiphersTable[7].dot11RSNAConfigPairwiseCipher =
            RSN_CIPHER_SUITE_WEP104;

    for (i = 0; i < MAX_NUM_SUPPORTED_CIPHER_SUITES; i ++) {
        prAdapter->rMib.dot11RSNAConfigPairwiseCiphersTable[i].dot11RSNAConfigPairwiseCipherEnabled =
            FALSE;
    }

    prAdapter->rMib.dot11RSNAConfigAuthenticationSuitesTable[0].dot11RSNAConfigAuthenticationSuite =
            WPA_AKM_SUITE_NONE;
    prAdapter->rMib.dot11RSNAConfigAuthenticationSuitesTable[1].dot11RSNAConfigAuthenticationSuite =
            WPA_AKM_SUITE_802_1X;
    prAdapter->rMib.dot11RSNAConfigAuthenticationSuitesTable[2].dot11RSNAConfigAuthenticationSuite =
            WPA_AKM_SUITE_PSK;
    prAdapter->rMib.dot11RSNAConfigAuthenticationSuitesTable[3].dot11RSNAConfigAuthenticationSuite =
            RSN_AKM_SUITE_NONE;
    prAdapter->rMib.dot11RSNAConfigAuthenticationSuitesTable[4].dot11RSNAConfigAuthenticationSuite =
            RSN_AKM_SUITE_802_1X;
    prAdapter->rMib.dot11RSNAConfigAuthenticationSuitesTable[5].dot11RSNAConfigAuthenticationSuite =
            RSN_AKM_SUITE_PSK;

#if CFG_SUPPORT_802_11W
    prAdapter->rMib.dot11RSNAConfigAuthenticationSuitesTable[6].dot11RSNAConfigAuthenticationSuite =
	    	RSN_AKM_SUITE_802_1X_SHA256;
    prAdapter->rMib.dot11RSNAConfigAuthenticationSuitesTable[7].dot11RSNAConfigAuthenticationSuite =
	    	RSN_AKM_SUITE_PSK_SHA256;
#endif

    for (i = 0; i < MAX_NUM_SUPPORTED_AKM_SUITES; i ++) {
        prAdapter->rMib.dot11RSNAConfigAuthenticationSuitesTable[i].dot11RSNAConfigAuthenticationSuiteEnabled =
            FALSE;
    }

    secClearPmkid(prAdapter);

    cnmTimerInitTimer(prAdapter,
                   &prAisSpecBssInfo->rPreauthenticationTimer,
                   (PFN_MGMT_TIMEOUT_FUNC)rsnIndicatePmkidCand,
                   (UINT_32)NULL);

#if CFG_SUPPORT_802_11W
    cnmTimerInitTimer(prAdapter,
                   &prAisSpecBssInfo->rSaQueryTimer,
                   (PFN_MGMT_TIMEOUT_FUNC)rsnStartSaQueryTimer,
                   (UINT_32)NULL);
#endif

    prAisSpecBssInfo->fgCounterMeasure = FALSE;
    prAisSpecBssInfo->ucWEPDefaultKeyID = 0;


    #if 0
    for (i=0;i<WTBL_SIZE;i++) {
        g_prWifiVar->arWtbl[i].fgUsed = FALSE;
        g_prWifiVar->arWtbl[i].prSta = NULL;
        g_prWifiVar->arWtbl[i].ucNetTypeIdx =  NETWORK_TYPE_INDEX_NUM;

    }
    nicPrivacyInitialize((UINT_8)NETWORK_TYPE_INDEX_NUM);
    #endif
}   /* secInit */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will indicate an Event of "Rx Class Error" to SEC_FSM for
*        JOIN Module.
*
* \param[in] prAdapter     Pointer to the Adapter structure
* \param[in] prSwRfb       Pointer to the SW RFB.
*
* \return FALSE                Class Error
*/
/*----------------------------------------------------------------------------*/
BOOL
secCheckClassError (
    IN P_ADAPTER_T          prAdapter,
    IN P_SW_RFB_T           prSwRfb,
    IN P_STA_RECORD_T       prStaRec
    )
{
    ASSERT(prAdapter);
    ASSERT(prSwRfb);
    //ASSERT(prStaRec);

    //prStaRec = &(g_arStaRec[prSwRfb->ucStaRecIdx]);

    if ((prStaRec) && 1 /* RXM_IS_DATA_FRAME(prSwRfb) */) {
        ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex = prStaRec->ucNetTypeIndex;

        if (IS_NET_ACTIVE(prAdapter, eNetTypeIndex)) {
        	  P_BSS_INFO_T prBssInfo;
        	  prBssInfo = &prAdapter->rWifiVar.arBssInfo[eNetTypeIndex];            

            if ((STA_STATE_3 != prStaRec->ucStaState) &&
            	   IS_BSS_ACTIVE(prBssInfo) && 
            	   prBssInfo->fgIsNetAbsent == FALSE) {
                /*(IS_AP_STA(prStaRec) || IS_CLIENT_STA(prStaRec))) {*/                

                if (WLAN_STATUS_SUCCESS == authSendDeauthFrame(prAdapter,
                                                               prStaRec,
                                                               NULL,
                                                               REASON_CODE_CLASS_3_ERR,
                                                               (PFN_TX_DONE_HANDLER)NULL)) {

                    DBGLOG(RSN, INFO, ("Send Deauth to MAC:["MACSTR"] for Rx Class 3 Error.\n",
                        MAC2STR(prStaRec->aucMacAddr)));
                }

                return FALSE;
            }

            return secRxPortControlCheck(prAdapter, prSwRfb);
        }
    }

    return FALSE;
} /* end of secCheckClassError() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to setting the sta port status.
*
* \param[in]  prAdapter Pointer to the Adapter structure
* \param[in]  prSta Pointer to the sta
* \param[in]  fgPortBlock The port status
*
* \retval none
*
*/
/*----------------------------------------------------------------------------*/
VOID
secSetPortBlocked (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta,
    IN BOOLEAN              fgPortBlock
   )
{
    if (prSta == NULL)
        return ;

    prSta->fgPortBlock = fgPortBlock;

    DBGLOG(RSN, TRACE, ("The STA "MACSTR" port %s\n", MAC2STR(prSta->aucMacAddr), fgPortBlock == TRUE ? "BLOCK" :" OPEN"));
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to report the sta port status.
*
* \param[in]  prAdapter Pointer to the Adapter structure
* \param[in]  prSta Pointer to the sta
* \param[out]  fgPortBlock The port status
*
* \return TRUE sta exist, FALSE sta not exist
*
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
secGetPortStatus (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta,
    OUT PBOOLEAN            pfgPortStatus
   )
{
    if (prSta == NULL)
        return FALSE;

    *pfgPortStatus = prSta->fgPortBlock;

    return TRUE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to handle Peer device Tx Security process MSDU.
*
* \param[in] prMsduInfo pointer to the packet info pointer
*
* \retval TRUE Accept the packet
* \retval FALSE Refuse the MSDU packet due port blocked
*
*/
/*----------------------------------------------------------------------------*/
BOOL /* ENUM_PORT_CONTROL_RESULT */
secTxPortControlCheck(
    IN P_ADAPTER_T          prAdapter,
    IN P_MSDU_INFO_T        prMsduInfo,
    IN P_STA_RECORD_T       prStaRec
    )
{
    ASSERT(prAdapter);
    ASSERT(prMsduInfo);
    ASSERT(prStaRec);

    if (prStaRec) {

        /* Todo:: */
        if (prMsduInfo->fgIs802_1x)
            return TRUE;

        if (prStaRec->fgPortBlock == TRUE) {
            DBGLOG(INIT, TRACE, ("Drop Tx packet due Port Control!\n"));
            return FALSE;
        }

#if CFG_SUPPORT_WAPI
        if (prAdapter->rWifiVar.rConnSettings.fgWapiMode) {
            return TRUE;
        }
#endif
        if (IS_STA_IN_AIS(prStaRec)) {
            if (!prAdapter->rWifiVar.rAisSpecificBssInfo.fgTransmitKeyExist &&
                (prAdapter->rWifiVar.rConnSettings.eEncStatus == ENUM_ENCRYPTION1_ENABLED)){
                DBGLOG(INIT, TRACE, ("Drop Tx packet due the key is removed!!!\n"));
                return FALSE;
            }
        }
    }

    return TRUE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to handle The Rx Security process MSDU.
*
* \param[in] prAdapter Pointer to the Adapter structure
* \param[in] prSWRfb SW rfb pinter
*
* \retval TRUE Accept the packet
* \retval FALSE Refuse the MSDU packet due port control
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
secRxPortControlCheck (
    IN P_ADAPTER_T          prAdapter,
    IN P_SW_RFB_T           prSWRfb
    )
{
    ASSERT(prSWRfb);

#if 0
    /* whsu:Todo: Process MGMT and DATA */
    if (prSWRfb->prStaRec) {
        if (prSWRfb->prStaRec->fgPortBlock == TRUE) {
            if (1 /* prSWRfb->fgIsDataFrame and not 1x*/ &&
                (g_prWifiVar->rConnSettings.eAuthMode >= AUTH_MODE_WPA)){
                //DBGLOG(SEC, WARN, ("Drop Rx data due port control !\r\n"));
                return TRUE; /* Todo: whsu FALSE; */
            }
            //if (!RX_STATUS_IS_PROTECT(prSWRfb->prRxStatus)) {
            //  DBGLOG(RSN, WARN, ("Drop rcv non-encrypted data frame!\n"));
            //  return FALSE;
            //}
        }
    }
    else {
    }
#endif
    return TRUE;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine will enable/disable the cipher suite
*
* \param[in] prAdapter Pointer to the adapter object data area.
* \param[in] u4CipherSuitesFlags flag for cipher suite
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID
secSetCipherSuite (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32     u4CipherSuitesFlags
    )
{
    UINT_32 i;
    P_DOT11_RSNA_CONFIG_PAIRWISE_CIPHERS_ENTRY prEntry;
    P_IEEE_802_11_MIB_T prMib;

    ASSERT(prAdapter);

    prMib = &prAdapter->rMib;

    ASSERT(prMib);

    if (u4CipherSuitesFlags == CIPHER_FLAG_NONE) {
        /* Disable all the pairwise cipher suites. */
        for (i = 0; i < MAX_NUM_SUPPORTED_CIPHER_SUITES; i++) {
            prMib->dot11RSNAConfigPairwiseCiphersTable[i].dot11RSNAConfigPairwiseCipherEnabled =
                FALSE;
        }

        /* Update the group cipher suite. */
        prMib->dot11RSNAConfigGroupCipher = WPA_CIPHER_SUITE_NONE;

        return;
    }

    for (i = 0; i < MAX_NUM_SUPPORTED_CIPHER_SUITES; i++) {
        prEntry = &prMib->dot11RSNAConfigPairwiseCiphersTable[i];

        switch (prEntry->dot11RSNAConfigPairwiseCipher) {
            case WPA_CIPHER_SUITE_WEP40:
            case RSN_CIPHER_SUITE_WEP40:
                 if (u4CipherSuitesFlags & CIPHER_FLAG_WEP40) {
                     prEntry->dot11RSNAConfigPairwiseCipherEnabled = TRUE;
                 }
                 else {
                     prEntry->dot11RSNAConfigPairwiseCipherEnabled = FALSE;
                 }
                 break;

            case WPA_CIPHER_SUITE_TKIP:
            case RSN_CIPHER_SUITE_TKIP:
                 if (u4CipherSuitesFlags & CIPHER_FLAG_TKIP) {
                     prEntry->dot11RSNAConfigPairwiseCipherEnabled = TRUE;
                 }
                 else {
                     prEntry->dot11RSNAConfigPairwiseCipherEnabled = FALSE;
                 }
                 break;

            case WPA_CIPHER_SUITE_CCMP:
            case RSN_CIPHER_SUITE_CCMP:
                 if (u4CipherSuitesFlags & CIPHER_FLAG_CCMP) {
                     prEntry->dot11RSNAConfigPairwiseCipherEnabled = TRUE;
                 }
                 else {
                     prEntry->dot11RSNAConfigPairwiseCipherEnabled = FALSE;
                 }
                 break;

            case WPA_CIPHER_SUITE_WEP104:
            case RSN_CIPHER_SUITE_WEP104:
                 if (u4CipherSuitesFlags & CIPHER_FLAG_WEP104) {
                     prEntry->dot11RSNAConfigPairwiseCipherEnabled = TRUE;
                 }
                 else {
                     prEntry->dot11RSNAConfigPairwiseCipherEnabled = FALSE;
                 }
                 break;
            default:
                 break;
        }
    }

    /* Update the group cipher suite. */
    if (rsnSearchSupportedCipher(prAdapter, WPA_CIPHER_SUITE_CCMP, &i)) {
        prMib->dot11RSNAConfigGroupCipher = WPA_CIPHER_SUITE_CCMP;
    }
    else if (rsnSearchSupportedCipher(prAdapter, WPA_CIPHER_SUITE_TKIP, &i)) {
        prMib->dot11RSNAConfigGroupCipher = WPA_CIPHER_SUITE_TKIP;
    }
    else if (rsnSearchSupportedCipher(prAdapter, WPA_CIPHER_SUITE_WEP104, &i)) {
        prMib->dot11RSNAConfigGroupCipher = WPA_CIPHER_SUITE_WEP104;
    }
    else if (rsnSearchSupportedCipher(prAdapter, WPA_CIPHER_SUITE_WEP40, &i)) {
        prMib->dot11RSNAConfigGroupCipher = WPA_CIPHER_SUITE_WEP40;
    }
    else {
        prMib->dot11RSNAConfigGroupCipher = WPA_CIPHER_SUITE_NONE;
    }

}   /* secSetCipherSuite */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to handle The 2nd Tx EAPoL Frame.
*
* \param[in] prAdapter Pointer to the Adapter structure
* \param[in] prMsduInfo pointer to the packet info pointer
* \param[in] pucPayload pointer to the 1x hdr
* \param[in] u2PayloadLen the 1x payload length
*
* \retval TRUE Accept the packet
* \retval FALSE Refuse the MSDU packet due port control
*
*/
/*----------------------------------------------------------------------------*/
BOOL
secProcessEAPOL (
    IN P_ADAPTER_T          prAdapter,
    IN P_MSDU_INFO_T        prMsduInfo,
    IN P_STA_RECORD_T       prStaRec,
    IN PUINT_8              pucPayload,
    IN UINT_16              u2PayloadLen
)
{
    P_EAPOL_KEY             prEapol = (P_EAPOL_KEY)NULL;
    P_IEEE_802_1X_HDR       pr1xHdr;
    UINT_16                 u2KeyInfo;

    ASSERT(prMsduInfo);
    ASSERT(prStaRec);

    //prStaRec = &(g_arStaRec[prMsduInfo->ucStaRecIndex]);
    ASSERT(prStaRec);

    if (prStaRec && IS_AP_STA(prStaRec)) {
        pr1xHdr = (P_IEEE_802_1X_HDR)pucPayload;
        if ((pr1xHdr->ucType == 3) /* EAPoL key */ && ((u2PayloadLen - 4) > sizeof(EAPOL_KEY))) {
            prEapol = (P_EAPOL_KEY)((PUINT_32)(pucPayload + 4));
            WLAN_GET_FIELD_BE16(prEapol->aucKeyInfo, &u2KeyInfo);
            if ((prEapol->ucType == 254) && (u2KeyInfo & MASK_2ND_EAPOL)) {
                if (u2KeyInfo & WPA_KEY_INFO_SECURE) {
                    /* 4th EAPoL check at secHandleTxDoneCallback() */
                    //DBGLOG(RSN, TRACE, ("Tx 4th EAPoL frame\r\n"));
                }
                else if (u2PayloadLen == 123 /* Not include LLC */) {
                    DBGLOG(RSN, INFO, ("Tx 2nd EAPoL frame\r\n"));
                    secFsmEvent2ndEapolTx(prAdapter, prStaRec);
                }
            }
        }
    }

    return TRUE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will handle the 4th EAPoL Tx done and mic Error Report frame.
*
* \param[in] prAdapter            Pointer to the Adapter structure
* \param[in] pMsduInfo            Pointer to the Msdu Info
* \param[in] rStatus                The Tx done status
*
* \return -
*/
/*----------------------------------------------------------------------------*/
VOID
secHandleTxDoneCallback(
    IN P_ADAPTER_T          prAdapter,
    IN P_MSDU_INFO_T        prMsduInfo,
    IN P_STA_RECORD_T       prStaRec,
    IN WLAN_STATUS          rStatus
    )
{
    PUINT_8                 pucPayload;
    P_IEEE_802_1X_HDR       pr1xHdr = (P_IEEE_802_1X_HDR)NULL;
    P_EAPOL_KEY             prEapol = (P_EAPOL_KEY)NULL;
    UINT_16                 u2KeyInfo;
    UINT_16                 u2PayloadLen;

    DEBUGFUNC("secHandleTxDoneCallback");

    ASSERT(prMsduInfo);
    //Todo:: Notice if using the TX free immediate after send to firmware, the payload may not correcttly!!!!

    ASSERT(prStaRec);

    //Todo:: This call back may not need because the order of set key and send 4th 1x can be make sure
    //Todo:: Notice the LLC offset
    #if 1
    pucPayload = (PUINT_8)prMsduInfo->prPacket;
    ASSERT(pucPayload);

    u2PayloadLen = prMsduInfo->u2FrameLength;

    if (0 /* prMsduInfo->fgIs1xFrame */) {

        if (prStaRec && IS_AP_STA(prStaRec)) {
            pr1xHdr = (P_IEEE_802_1X_HDR)(PUINT_32)(pucPayload + 8);
            if ((pr1xHdr->ucType == 3) /* EAPoL key */ && ((u2PayloadLen - 4) > sizeof(EAPOL_KEY))) {
                prEapol = (P_EAPOL_KEY)(PUINT_32)(pucPayload + 12);
                WLAN_GET_FIELD_BE16(prEapol->aucKeyInfo, &u2KeyInfo);
                if ((prEapol->ucType == 254) && (u2KeyInfo & MASK_2ND_EAPOL)) {
                    if (prStaRec->rSecInfo.fg2nd1xSend == TRUE && u2PayloadLen == 107 /* include LLC *//* u2KeyInfo & WPA_KEY_INFO_SECURE */) {
                        DBGLOG(RSN, INFO, ("Tx 4th EAPoL frame\r\n"));
                        secFsmEvent4ndEapolTxDone(prAdapter, prStaRec);
                    }
                    else if (prAdapter->rWifiVar.rAisSpecificBssInfo.fgCheckEAPoLTxDone){
                        DBGLOG(RSN, INFO, ("Tx EAPoL Error report frame\r\n"));
                        //secFsmEventEapolTxDone(prAdapter, (UINT_32)prMsduInfo->prStaRec);
                    }
                }
            }
        }

    }
    #endif
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to initialize the pmkid parameters.
*
* \param[in] prAdapter Pointer to the Adapter structure
*
* \retval NONE
*/
/*----------------------------------------------------------------------------*/
VOID
secClearPmkid (
    IN P_ADAPTER_T          prAdapter
    )
{
    P_AIS_SPECIFIC_BSS_INFO_T prAisSpecBssInfo;

    DEBUGFUNC("secClearPmkid");

    prAisSpecBssInfo = &prAdapter->rWifiVar.rAisSpecificBssInfo;
    DBGLOG(RSN, TRACE, ("secClearPmkid\n"));
    prAisSpecBssInfo->u4PmkidCandicateCount = 0;
    prAisSpecBssInfo->u4PmkidCacheCount = 0;
    kalMemZero((PVOID)prAisSpecBssInfo->arPmkidCandicate, sizeof(PMKID_CANDICATE_T) * CFG_MAX_PMKID_CACHE);
    kalMemZero((PVOID)prAisSpecBssInfo->arPmkidCache, sizeof(PMKID_ENTRY_T) * CFG_MAX_PMKID_CACHE);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief Whether WPA, or WPA2 but not WPA-None is enabled.
*
* \param[in] prAdapter Pointer to the Adapter structure
*
* \retval BOOLEAN
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
secRsnKeyHandshakeEnabled (
    IN P_ADAPTER_T          prAdapter
    )
{
    P_CONNECTION_SETTINGS_T prConnSettings;

    ASSERT(prAdapter);

    prConnSettings = &prAdapter->rWifiVar.rConnSettings;

    ASSERT(prConnSettings);

    ASSERT(prConnSettings->eEncStatus < ENUM_ENCRYPTION3_KEY_ABSENT);

    if (prConnSettings->eEncStatus == ENUM_ENCRYPTION_DISABLED) {
        return FALSE;
    }

    ASSERT(prConnSettings->eAuthMode < AUTH_MODE_NUM);
    if ((prConnSettings->eAuthMode >= AUTH_MODE_WPA) &&
        (prConnSettings->eAuthMode != AUTH_MODE_WPA_NONE)) {
        return TRUE;
    }

    return FALSE;
} /* secRsnKeyHandshakeEnabled */


/*----------------------------------------------------------------------------*/
/*!
* \brief Return whether the transmit key alread installed.
*
* \param[in] prAdapter Pointer to the Adapter structure
* \param[in] prSta Pointer the sta record
*
* \retval TRUE Default key or Transmit key installed
*         FALSE Default key or Transmit key not installed
*
* \note:
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
secTransmitKeyExist (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{
    ASSERT(prSta);

    if (prSta->fgTransmitKeyExist){
        return TRUE;
    }
    else {
        return FALSE;
    }
} /* secTransmitKeyExist */


/*----------------------------------------------------------------------------*/
/*!
* \brief Whether 802.11 privacy is enabled.
*
* \param[in] prAdapter Pointer to the Adapter structure
*
* \retval BOOLEAN
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
secEnabledInAis (
    IN P_ADAPTER_T          prAdapter
    )
{
    DEBUGFUNC("secEnabled");

    ASSERT(prAdapter->rWifiVar.rConnSettings.eEncStatus < ENUM_ENCRYPTION3_KEY_ABSENT);

    switch (prAdapter->rWifiVar.rConnSettings.eEncStatus) {
        case ENUM_ENCRYPTION_DISABLED:
            return FALSE;
        case ENUM_ENCRYPTION1_ENABLED:
        case ENUM_ENCRYPTION2_ENABLED:
        case ENUM_ENCRYPTION3_ENABLED:
            return TRUE;
        default:
            DBGLOG(RSN, TRACE, ("Unknown encryption setting %d\n",
                prAdapter->rWifiVar.rConnSettings.eEncStatus));
            break;
    }
    return FALSE;
} /* secEnabled */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set the privacy bit at mac header for TxM
*
* \param[in] prAdapter Pointer to the Adapter structure
* \param[in] prMsdu the msdu for known the sta record
*
* \return TRUE the privacy need to set
*            FALSE the privacy no need to set
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
secIsProtectedFrame (
    IN P_ADAPTER_T          prAdapter,
    IN P_MSDU_INFO_T        prMsdu,
    IN P_STA_RECORD_T       prStaRec
    )
{
    ASSERT(prAdapter);

    ASSERT(prMsdu);

    ASSERT(prStaRec);
    //prStaRec = &(g_arStaRec[prMsdu->ucStaRecIndex]);

    if (prStaRec == NULL) {
        if (prAdapter->rWifiVar.rAisSpecificBssInfo.fgTransmitKeyExist)
            return TRUE;
        return FALSE; /* No privacy bit */
    }

    /* Todo:: */
    if (0 /* prMsdu->fgIs1xFrame */){
        if (IS_STA_IN_AIS(prStaRec) &&
            prAdapter->rWifiVar.rConnSettings.eAuthMode < AUTH_MODE_WPA) {
            DBGLOG(RSN, LOUD, ("For AIS Legacy 1x, always not encryped\n"));
            return FALSE;
        }
        else if (!prStaRec->fgTransmitKeyExist) {
            DBGLOG(RSN, LOUD, ("1x Not Protected.\n"));
            return FALSE;
        }
        else if (prStaRec->rSecInfo.fgKeyStored) {
            DBGLOG(RSN, LOUD, ("1x not Protected due key stored!\n"));
            return FALSE;
        }
        else {
            DBGLOG(RSN, LOUD, ("1x Protected.\n"));
            return TRUE;
        }
    }
    else {
        if (!prStaRec->fgTransmitKeyExist) {
            /* whsu , check for AIS only */
            if (prAdapter->rWifiVar.rConnSettings.eAuthMode < AUTH_MODE_WPA &&
                prAdapter->rWifiVar.rAisSpecificBssInfo.fgTransmitKeyExist){
                DBGLOG(RSN, LOUD, ("Protected\n"));
                return TRUE;
            }
        }
        else {
            DBGLOG(RSN, LOUD, ("Protected.\n"));
            return TRUE;
        }
    }

    /* No sec or key is removed!!! */
    return FALSE;
}
#endif
