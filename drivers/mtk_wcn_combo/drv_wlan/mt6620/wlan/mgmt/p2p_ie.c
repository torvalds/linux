#include "p2p_precomp.h"

#if CFG_SUPPORT_WFD
#if CFG_SUPPORT_WFD_COMPOSE_IE
#if 0
APPEND_VAR_ATTRI_ENTRY_T txProbeRspWFDAttributesTable[] = {
     { (WFD_ATTRI_HDR_LEN + WFD_ATTRI_MAX_LEN_DEV_INFO)   ,    NULL,                           wfdFuncAppendAttriDevInfo        }  /* 0 */
    ,{ (WFD_ATTRI_HDR_LEN + WFD_ATTRI_MAX_LEN_ASSOC_BSSID),    NULL,                           wfdFuncAppendAttriAssocBssid   }  /* 1 */
    ,{ (WFD_ATTRI_HDR_LEN + WFD_ATTRI_MAX_LEN_COUPLED_SINK_INFO),    NULL,                           wfdFuncAppendAttriCoupledSinkInfo   }  /* 6 */
    ,{ (WFD_ATTRI_HDR_LEN + WFD_ATTRI_MAX_LEN_EXT_CAPABILITY),    NULL,                           wfdFuncAppendAttriExtCapability   }  /* 7 */
    ,{ 0,                                                            wfdFuncCalculateAttriLenSessionInfo,                           wfdFuncAppendAttriSessionInfo   }  /* 9 */
};

APPEND_VAR_ATTRI_ENTRY_T txBeaconWFDAttributesTable[] = {
     { (WFD_ATTRI_HDR_LEN + WFD_ATTRI_MAX_LEN_DEV_INFO)   ,    NULL,                           wfdFuncAppendAttriDevInfo        }  /* 0 */
    ,{ (WFD_ATTRI_HDR_LEN + WFD_ATTRI_MAX_LEN_ASSOC_BSSID),    NULL,                           wfdFuncAppendAttriAssocBssid   }  /* 1 */
    ,{ (WFD_ATTRI_HDR_LEN + WFD_ATTRI_MAX_LEN_COUPLED_SINK_INFO),    NULL,                           wfdFuncAppendAttriCoupledSinkInfo   }  /* 6 */
};

APPEND_VAR_ATTRI_ENTRY_T txAssocReqWFDAttributesTable[] = {
     { (WFD_ATTRI_HDR_LEN + WFD_ATTRI_MAX_LEN_DEV_INFO)   ,    NULL,                           wfdFuncAppendAttriDevInfo        }  /* 0 */
    ,{ (WFD_ATTRI_HDR_LEN + WFD_ATTRI_MAX_LEN_ASSOC_BSSID),    NULL,                           wfdFuncAppendAttriAssocBssid   }  /* 1 */
    ,{ (WFD_ATTRI_HDR_LEN + WFD_ATTRI_MAX_LEN_COUPLED_SINK_INFO),    NULL,                           wfdFuncAppendAttriCoupledSinkInfo   }  /* 6 */
};
#endif

APPEND_VAR_ATTRI_ENTRY_T txAssocRspWFDAttributesTable[] = {
     { (WFD_ATTRI_HDR_LEN + WFD_ATTRI_MAX_LEN_DEV_INFO)   ,    NULL,                           wfdFuncAppendAttriDevInfo        }  /* 0 */
    ,{ (WFD_ATTRI_HDR_LEN + WFD_ATTRI_MAX_LEN_ASSOC_BSSID),    NULL,                           wfdFuncAppendAttriAssocBssid   }  /* 1 */
    ,{ (WFD_ATTRI_HDR_LEN + WFD_ATTRI_MAX_LEN_COUPLED_SINK_INFO),    NULL,                           wfdFuncAppendAttriCoupledSinkInfo   }  /* 6 */
    ,{ 0,                                                            wfdFuncCalculateAttriLenSessionInfo,                           wfdFuncAppendAttriSessionInfo   }  /* 9 */

};

#endif




UINT_32
p2pCalculate_IEForAssocReq (

    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    )
{
    P_P2P_FSM_INFO_T prP2pFsmInfo = (P_P2P_FSM_INFO_T)NULL;
    P_P2P_CONNECTION_REQ_INFO_T prConnReqInfo = (P_P2P_CONNECTION_REQ_INFO_T)NULL;
    UINT_32 u4RetValue = 0;

    do {
        ASSERT_BREAK((eNetTypeIndex == NETWORK_TYPE_P2P_INDEX) && (prAdapter != NULL));

        prP2pFsmInfo = prAdapter->rWifiVar.prP2pFsmInfo;

        prConnReqInfo = &(prP2pFsmInfo->rConnReqInfo);

        u4RetValue = prConnReqInfo->u4BufLength;

        // ADD HT Capability
        u4RetValue += (ELEM_HDR_LEN + ELEM_MAX_LEN_HT_CAP);
		
		// ADD WMM Information Element
        u4RetValue += (ELEM_HDR_LEN + ELEM_MAX_LEN_WMM_INFO);

    } while (FALSE);

    return u4RetValue;
} /* p2pCalculate_IEForAssocReq */



/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to generate P2P IE for Beacon frame.
*
* @param[in] prMsduInfo             Pointer to the composed MSDU_INFO_T.
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
p2pGenerate_IEForAssocReq (
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    )
{
    P_P2P_FSM_INFO_T prP2pFsmInfo = (P_P2P_FSM_INFO_T)NULL;
    P_P2P_CONNECTION_REQ_INFO_T prConnReqInfo = (P_P2P_CONNECTION_REQ_INFO_T)NULL;
    PUINT_8 pucIEBuf = (PUINT_8)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prMsduInfo != NULL));

        prP2pFsmInfo = prAdapter->rWifiVar.prP2pFsmInfo;

        prConnReqInfo = &(prP2pFsmInfo->rConnReqInfo);

        pucIEBuf = (PUINT_8)((UINT_32)prMsduInfo->prPacket + (UINT_32)prMsduInfo->u2FrameLength);

        kalMemCopy(pucIEBuf, prConnReqInfo->aucIEBuf, prConnReqInfo->u4BufLength);

        prMsduInfo->u2FrameLength += prConnReqInfo->u4BufLength;

        rlmReqGenerateHtCapIE (prAdapter,prMsduInfo);
		mqmGenerateWmmInfoIE (prAdapter,prMsduInfo);


    } while (FALSE);

    return;

} /* p2pGenerate_IEForAssocReq */



UINT_32
wfdFuncAppendAttriDevInfo (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    )
{
    UINT_32 u4AttriLen = 0;
    PUINT_8 pucBuffer = NULL;
    P_WFD_DEVICE_INFORMATION_IE_T prWfdDevInfo = (P_WFD_DEVICE_INFORMATION_IE_T)NULL;
    P_WFD_CFG_SETTINGS_T prWfdCfgSettings = (P_WFD_CFG_SETTINGS_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (pucBuf != NULL) && (pu2Offset != NULL));

        prWfdCfgSettings = &(prAdapter->rWifiVar.prP2pFsmInfo->rWfdConfigureSettings);

        ASSERT_BREAK((prWfdCfgSettings != NULL));

        if ((prWfdCfgSettings->ucWfdEnable == 0) ||
                ((prWfdCfgSettings->u4WfdFlag & WFD_FLAGS_DEV_INFO_VALID) == 0)) {
            break;
        }

        pucBuffer = (PUINT_8)((UINT_32)pucBuf + (UINT_32)(*pu2Offset));

        ASSERT_BREAK(pucBuffer != NULL);

        prWfdDevInfo = (P_WFD_DEVICE_INFORMATION_IE_T)pucBuffer;

        prWfdDevInfo->ucElemID = WFD_ATTRI_ID_DEV_INFO;

        WLAN_SET_FIELD_BE16(&prWfdDevInfo->u2WfdDevInfo, prWfdCfgSettings->u2WfdDevInfo);

        WLAN_SET_FIELD_BE16(&prWfdDevInfo->u2SessionMgmtCtrlPort, prWfdCfgSettings->u2WfdControlPort);

        WLAN_SET_FIELD_BE16(&prWfdDevInfo->u2WfdDevMaxSpeed, prWfdCfgSettings->u2WfdMaximumTp);

        WLAN_SET_FIELD_BE16(&prWfdDevInfo->u2Length, WFD_ATTRI_MAX_LEN_DEV_INFO);

        u4AttriLen = WFD_ATTRI_MAX_LEN_DEV_INFO + WFD_ATTRI_HDR_LEN;

    } while (FALSE);

    (*pu2Offset) += (UINT_16)u4AttriLen;

    return u4AttriLen;
}
/* wfdFuncAppendAttriDevInfo */

UINT_32
wfdFuncAppendAttriAssocBssid (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    )
{
    UINT_32 u4AttriLen = 0;
    PUINT_8 pucBuffer = NULL;
    P_WFD_ASSOCIATED_BSSID_IE_T prWfdAssocBssid = (P_WFD_ASSOCIATED_BSSID_IE_T)NULL;
    P_WFD_CFG_SETTINGS_T prWfdCfgSettings = (P_WFD_CFG_SETTINGS_T)NULL;
    P_BSS_INFO_T prAisBssInfo = (P_BSS_INFO_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (pucBuf != NULL) && (pu2Offset != NULL));

        prWfdCfgSettings = &(prAdapter->rWifiVar.prP2pFsmInfo->rWfdConfigureSettings);

        ASSERT_BREAK((prWfdCfgSettings != NULL));

        if (prWfdCfgSettings->ucWfdEnable == 0) {
            break;
        }

        /* AIS network. */
        prAisBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);

        if ((!IS_NET_ACTIVE(prAdapter, NETWORK_TYPE_AIS_INDEX)) ||
                (prAisBssInfo->eConnectionState != PARAM_MEDIA_STATE_CONNECTED)) {
            break;
        }

        pucBuffer = (PUINT_8)((UINT_32)pucBuf + (UINT_32)(*pu2Offset));

        ASSERT_BREAK(pucBuffer != NULL);

        prWfdAssocBssid = (P_WFD_ASSOCIATED_BSSID_IE_T)pucBuffer;

        prWfdAssocBssid->ucElemID = WFD_ATTRI_ID_ASSOC_BSSID;

        WLAN_SET_FIELD_BE16(&prWfdAssocBssid->u2Length, WFD_ATTRI_MAX_LEN_ASSOC_BSSID);

        COPY_MAC_ADDR(prWfdAssocBssid->aucAssocBssid, prAisBssInfo->aucBSSID);

        u4AttriLen = WFD_ATTRI_MAX_LEN_ASSOC_BSSID + WFD_ATTRI_HDR_LEN;

    } while (FALSE);

    (*pu2Offset) += (UINT_16)u4AttriLen;

    return u4AttriLen;
}
/* wfdFuncAppendAttriAssocBssid */

UINT_32
wfdFuncAppendAttriCoupledSinkInfo (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    )
{
    UINT_32 u4AttriLen = 0;
    PUINT_8 pucBuffer = NULL;
    P_WFD_COUPLE_SINK_INFORMATION_IE_T prWfdCoupleSinkInfo = (P_WFD_COUPLE_SINK_INFORMATION_IE_T)NULL;
    P_WFD_CFG_SETTINGS_T prWfdCfgSettings = (P_WFD_CFG_SETTINGS_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (pucBuf != NULL) && (pu2Offset != NULL));

        prWfdCfgSettings = &(prAdapter->rWifiVar.prP2pFsmInfo->rWfdConfigureSettings);

        ASSERT_BREAK((prWfdCfgSettings != NULL));

        if ((prWfdCfgSettings->ucWfdEnable == 0) ||
                ((prWfdCfgSettings->u4WfdFlag & WFD_FLAGS_SINK_INFO_VALID) == 0)) {
            break;
        }

        pucBuffer = (PUINT_8)((UINT_32)pucBuf + (UINT_32)(*pu2Offset));

        ASSERT_BREAK(pucBuffer != NULL);

        prWfdCoupleSinkInfo = (P_WFD_COUPLE_SINK_INFORMATION_IE_T)pucBuffer;

        prWfdCoupleSinkInfo->ucElemID = WFD_ATTRI_ID_COUPLED_SINK_INFO;

        WLAN_SET_FIELD_BE16(&prWfdCoupleSinkInfo->u2Length, WFD_ATTRI_MAX_LEN_COUPLED_SINK_INFO);

        COPY_MAC_ADDR(prWfdCoupleSinkInfo->aucCoupleSinkMac, prWfdCfgSettings->aucWfdCoupleSinkAddress);

        prWfdCoupleSinkInfo->ucCoupleSinkStatusBp = prWfdCfgSettings->ucWfdCoupleSinkStatus;

        u4AttriLen = WFD_ATTRI_MAX_LEN_COUPLED_SINK_INFO + WFD_ATTRI_HDR_LEN;

    } while (FALSE);

    (*pu2Offset) += (UINT_16)u4AttriLen;

    return u4AttriLen;
}
/* wfdFuncAppendAttriCoupledSinkInfo */

UINT_32
wfdFuncAppendAttriExtCapability (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    )
{
    UINT_32 u4AttriLen = 0;
    PUINT_8 pucBuffer = NULL;
    P_WFD_EXTENDED_CAPABILITY_IE_T prWfdExtCapability = (P_WFD_EXTENDED_CAPABILITY_IE_T)NULL;
    P_WFD_CFG_SETTINGS_T prWfdCfgSettings = (P_WFD_CFG_SETTINGS_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (pucBuf != NULL) && (pu2Offset != NULL));

        prWfdCfgSettings = &(prAdapter->rWifiVar.prP2pFsmInfo->rWfdConfigureSettings);

        ASSERT_BREAK((prWfdCfgSettings != NULL));

        if ((prWfdCfgSettings->ucWfdEnable == 0) ||
                ((prWfdCfgSettings->u4WfdFlag & WFD_FLAGS_EXT_CAPABILITY_VALID) == 0)) {
            break;
        }

        pucBuffer = (PUINT_8)((UINT_32)pucBuf + (UINT_32)(*pu2Offset));

        ASSERT_BREAK(pucBuffer != NULL);

        prWfdExtCapability = (P_WFD_EXTENDED_CAPABILITY_IE_T)pucBuffer;

        prWfdExtCapability->ucElemID = WFD_ATTRI_ID_EXT_CAPABILITY;

        WLAN_SET_FIELD_BE16(&prWfdExtCapability->u2Length, WFD_ATTRI_MAX_LEN_EXT_CAPABILITY);

        WLAN_SET_FIELD_BE16(&prWfdExtCapability->u2WfdExtCapabilityBp, prWfdCfgSettings->u2WfdExtendCap);

        u4AttriLen = WFD_ATTRI_MAX_LEN_EXT_CAPABILITY + WFD_ATTRI_HDR_LEN;

    } while (FALSE);

    (*pu2Offset) += (UINT_16)u4AttriLen;

    return u4AttriLen;
}
/* wfdFuncAppendAttriExtCapability */



/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to calculate length of Channel List Attribute
*
* @param[in] prStaRec           Pointer to the STA_RECORD_T
*
* @return The length of Attribute added
*/
/*----------------------------------------------------------------------------*/
UINT_32
wfdFuncCalculateAttriLenSessionInfo (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    )
{
    UINT_16 u2AttriLen = 0;
    P_WFD_CFG_SETTINGS_T prWfdCfgSettings = (P_WFD_CFG_SETTINGS_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prStaRec != NULL));

        prWfdCfgSettings = &(prAdapter->rWifiVar.prP2pFsmInfo->rWfdConfigureSettings);

        if (prWfdCfgSettings->ucWfdEnable == 0) {
            break;
        }

        u2AttriLen = prWfdCfgSettings->u2WfdSessionInformationIELen + WFD_ATTRI_HDR_LEN;

    } while (FALSE);


    return (UINT_32)u2AttriLen;

} /* wfdFuncCalculateAttriLenSessionInfo */


UINT_32
wfdFuncAppendAttriSessionInfo (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize
    )
{
    UINT_32 u4AttriLen = 0;
    PUINT_8 pucBuffer = NULL;
    P_WFD_SESSION_INFORMATION_IE_T prWfdSessionInfo = (P_WFD_SESSION_INFORMATION_IE_T)NULL;
    P_WFD_CFG_SETTINGS_T prWfdCfgSettings = (P_WFD_CFG_SETTINGS_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (pucBuf != NULL) && (pu2Offset != NULL));

        prWfdCfgSettings = &(prAdapter->rWifiVar.prP2pFsmInfo->rWfdConfigureSettings);

        ASSERT_BREAK((prWfdCfgSettings != NULL));

        if ((prWfdCfgSettings->ucWfdEnable == 0) || (prWfdCfgSettings->u2WfdSessionInformationIELen == 0)) {
            break;
        }

        pucBuffer = (PUINT_8)((UINT_32)pucBuf + (UINT_32)(*pu2Offset));

        ASSERT_BREAK(pucBuffer != NULL);

        prWfdSessionInfo = (P_WFD_SESSION_INFORMATION_IE_T)pucBuffer;

        prWfdSessionInfo->ucElemID = WFD_ATTRI_ID_SESSION_INFO;

        // TODO: Check endian issue?
        kalMemCopy(prWfdSessionInfo->pucWfdDevInfoDesc, prWfdCfgSettings->aucWfdSessionInformationIE, prWfdCfgSettings->u2WfdSessionInformationIELen);

        WLAN_SET_FIELD_16(&prWfdSessionInfo->u2Length, prWfdCfgSettings->u2WfdSessionInformationIELen);

        u4AttriLen = prWfdCfgSettings->u2WfdSessionInformationIELen + WFD_ATTRI_HDR_LEN;

    }
while (FALSE);

    (*pu2Offset) += (UINT_16)u4AttriLen;

    return u4AttriLen;
}
/* wfdFuncAppendAttriSessionInfo */



#if CFG_SUPPORT_WFD_COMPOSE_IE
VOID
wfdFuncGenerateWfd_IE (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsAssocFrame,
    IN PUINT_16 pu2Offset,
    IN PUINT_8 pucBuf,
    IN UINT_16 u2BufSize,
    IN APPEND_VAR_ATTRI_ENTRY_T arAppendAttriTable[],
    IN UINT_32 u4AttriTableSize
    )
{


    PUINT_8 pucBuffer = (PUINT_8)NULL;
    P_IE_WFD_T prIeWFD = (P_IE_WFD_T)NULL;
    UINT_32 u4OverallAttriLen;
    UINT_32 u4AttriLen;
    UINT_8 aucWfaOui[] = VENDOR_OUI_WFA_SPECIFIC;
    UINT_8 aucTempBuffer[P2P_MAXIMUM_ATTRIBUTE_LEN];
    UINT_32 i;


    do {
        ASSERT_BREAK((prAdapter != NULL) && (pucBuf != NULL));

        pucBuffer = (PUINT_8)((UINT_32)pucBuf + (*pu2Offset));

        ASSERT_BREAK(pucBuffer != NULL);

        /* Check buffer length is still enough. */
        ASSERT_BREAK((u2BufSize - (*pu2Offset)) >= WFD_IE_OUI_HDR);

        prIeWFD = (P_IE_WFD_T)pucBuffer;

        prIeWFD->ucId = ELEM_ID_WFD;

        prIeWFD->aucOui[0] = aucWfaOui[0];
        prIeWFD->aucOui[1] = aucWfaOui[1];
        prIeWFD->aucOui[2] = aucWfaOui[2];
        prIeWFD->ucOuiType = VENDOR_OUI_TYPE_WFD;

        (*pu2Offset) += WFD_IE_OUI_HDR;

        /* Overall length of all Attributes */
        u4OverallAttriLen = 0;

        for (i = 0; i < u4AttriTableSize; i++) {

            if (arAppendAttriTable[i].pfnAppendAttri) {
                u4AttriLen = arAppendAttriTable[i].pfnAppendAttri(prAdapter, fgIsAssocFrame, pu2Offset, pucBuf, u2BufSize);

                u4OverallAttriLen += u4AttriLen;

                if (u4OverallAttriLen > P2P_MAXIMUM_ATTRIBUTE_LEN) {
                    u4OverallAttriLen -= P2P_MAXIMUM_ATTRIBUTE_LEN;

                    prIeWFD->ucLength = (VENDOR_OUI_TYPE_LEN + P2P_MAXIMUM_ATTRIBUTE_LEN);

                    pucBuffer = (PUINT_8)((UINT_32)prIeWFD + (WFD_IE_OUI_HDR + P2P_MAXIMUM_ATTRIBUTE_LEN));

                    prIeWFD = (P_IE_WFD_T)((UINT_32)prIeWFD + (WFD_IE_OUI_HDR + P2P_MAXIMUM_ATTRIBUTE_LEN));

                    kalMemCopy(aucTempBuffer, pucBuffer, u4OverallAttriLen);

                    prIeWFD->ucId = ELEM_ID_WFD;

                    prIeWFD->aucOui[0] = aucWfaOui[0];
                    prIeWFD->aucOui[1] = aucWfaOui[1];
                    prIeWFD->aucOui[2] = aucWfaOui[2];
                    prIeWFD->ucOuiType = VENDOR_OUI_TYPE_WFD;

                    kalMemCopy(prIeWFD->aucWFDAttributes, aucTempBuffer, u4OverallAttriLen);
                    (*pu2Offset) += WFD_IE_OUI_HDR;
                }

            }

        }

        prIeWFD->ucLength = (UINT_8)(VENDOR_OUI_TYPE_LEN + u4OverallAttriLen);


    } while (FALSE);

    return;
} /* wfdFuncGenerateWfd_IE */

#endif /* CFG_SUPPORT_WFD_COMPOSE_IE */




UINT_32
wfdFuncCalculateWfdIELenForAssocRsp (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    )
{

#if CFG_SUPPORT_WFD_COMPOSE_IE
    P_WFD_CFG_SETTINGS_T prWfdCfgSettings = (P_WFD_CFG_SETTINGS_T)NULL;

    prWfdCfgSettings = &(prAdapter->rWifiVar.prP2pFsmInfo->rWfdConfigureSettings);

    if (
        !IS_STA_P2P_TYPE(prStaRec) ||
            (prWfdCfgSettings->ucWfdEnable == 0)) {
        return 0;
    }

    return p2pFuncCalculateP2P_IELen(prAdapter,
                                eNetTypeIndex,
                                prStaRec,
                                txAssocRspWFDAttributesTable,
                                sizeof(txAssocRspWFDAttributesTable)/sizeof(APPEND_VAR_ATTRI_ENTRY_T));
    
#else
    return 0;
#endif
} /* wfdFuncCalculateWfdIELenForAssocRsp */



VOID
wfdFuncGenerateWfdIEForAssocRsp (
    IN  P_ADAPTER_T       prAdapter,
    IN  P_MSDU_INFO_T     prMsduInfo
    )
{

#if CFG_SUPPORT_WFD_COMPOSE_IE
    P_WFD_CFG_SETTINGS_T prWfdCfgSettings = (P_WFD_CFG_SETTINGS_T)NULL;
    P_STA_RECORD_T prStaRec;
    
    do {
        ASSERT_BREAK((prMsduInfo != NULL) && (prAdapter != NULL));
    
        prWfdCfgSettings = &(prAdapter->rWifiVar.prP2pFsmInfo->rWfdConfigureSettings);
        prStaRec = cnmGetStaRecByIndex(prAdapter, prMsduInfo->ucStaRecIndex);

        if (IS_STA_P2P_TYPE(prStaRec)) {
       
            if (prWfdCfgSettings->ucWfdEnable == 0) {
                break;
            }
            if((prWfdCfgSettings->u4WfdFlag & WFD_FLAGS_DEV_INFO_VALID) == 0) {
                break;
            }

            wfdFuncGenerateWfd_IE(prAdapter,
                            FALSE,
                            &prMsduInfo->u2FrameLength,
                            prMsduInfo->prPacket,
                            1500,
                            txAssocRspWFDAttributesTable,
                            sizeof(txAssocRspWFDAttributesTable)/sizeof(APPEND_VAR_ATTRI_ENTRY_T));
        }
    } while (FALSE);

    return;
#else

    return;
#endif
} /* wfdFuncGenerateWfdIEForAssocRsp */

#endif
