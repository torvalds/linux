/*
** $Id: @(#) p2p_scan.c@@
*/

/*! \file   "p2p_scan.c"
    \brief  This file defines the p2p scan profile and the processing function of
            scan result for SCAN Module.

    The SCAN Profile selection is part of SCAN MODULE and responsible for defining
    SCAN Parameters - e.g. MIN_CHANNEL_TIME, number of scan channels.
    In this file we also define the process of SCAN Result including adding, searching
    and removing SCAN record from the list.
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

P_P2P_DEVICE_DESC_T
scanSearchTargetP2pDesc (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 aucDeviceID[],
    IN PP_BSS_DESC_T pprBssDesc
    )
{

    P_P2P_DEVICE_DESC_T prTargetP2pDesc = (P_P2P_DEVICE_DESC_T)NULL;
    P_SCAN_INFO_T prScanInfo = (P_SCAN_INFO_T)NULL;
    P_LINK_T prBSSDescList;
    P_BSS_DESC_T prBssDesc = (P_BSS_DESC_T)NULL;


    ASSERT(prAdapter);
    ASSERT(aucDeviceID);

    prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
    prBSSDescList = &prScanInfo->rBSSDescList;

    //4 <1> The outer loop to search for a candidate.
    LINK_FOR_EACH_ENTRY(prBssDesc, prBSSDescList, rLinkEntry, BSS_DESC_T) {

        /* Loop for each prBssDesc */
        prTargetP2pDesc = scanFindP2pDeviceDesc(prAdapter,
                                                prBssDesc,
                                                aucDeviceID,
                                                TRUE,
                                                FALSE);

        if (prTargetP2pDesc != NULL) {
            break;
        }
    }

    if ((pprBssDesc) && (prTargetP2pDesc != NULL)) {
        /* Only valid if prTargetP2pDesc is not NULL. */
        *pprBssDesc = prBssDesc;
    }

    return prTargetP2pDesc;
} /* scanSearchTargetP2pDesc */




VOID
scanInvalidAllP2pClientDevice (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc
    )
{
    P_LINK_ENTRY_T prLinkEntry = (P_LINK_ENTRY_T)NULL;
    P_P2P_DEVICE_DESC_T prTargetDesc = (P_P2P_DEVICE_DESC_T)NULL;

    LINK_FOR_EACH(prLinkEntry, &prBssDesc->rP2pDeviceList) {
        prTargetDesc = LINK_ENTRY(prLinkEntry, P2P_DEVICE_DESC_T, rLinkEntry);

        if (prTargetDesc->fgDevInfoValid) {
            prTargetDesc->fgDevInfoValid = FALSE;
        }
    }

    return;
} /* scanRenewP2pClientDevice */

VOID
scanRemoveInvalidP2pClientDevice (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc
    )
{
    P_LINK_ENTRY_T prLinkEntry = (P_LINK_ENTRY_T)NULL, prNexEntry = (P_LINK_ENTRY_T)NULL;
    P_P2P_DEVICE_DESC_T prTargetDesc = (P_P2P_DEVICE_DESC_T)NULL;
    P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = (P_P2P_CONNECTION_SETTINGS_T)NULL;

    prP2pConnSettings = prAdapter->rWifiVar.prP2PConnSettings;

    LINK_FOR_EACH_SAFE(prLinkEntry, prNexEntry, &prBssDesc->rP2pDeviceList) {
        prTargetDesc = LINK_ENTRY(prLinkEntry, P2P_DEVICE_DESC_T, rLinkEntry);

        if (!prTargetDesc->fgDevInfoValid) {
            LINK_REMOVE_KNOWN_ENTRY(&prBssDesc->rP2pDeviceList, prLinkEntry);
            if ((prP2pConnSettings) &&
                    (prP2pConnSettings->prTargetP2pDesc == prTargetDesc)) {
                prP2pConnSettings->prTargetP2pDesc = NULL;
            }
            kalMemFree(prTargetDesc, VIR_MEM_TYPE, sizeof(P2P_DEVICE_DESC_T));
        }
    }

    return;
} /* scanRenewP2pClientDevice */



P_P2P_DEVICE_DESC_T
scanFindP2pDeviceDesc (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc,
    IN UINT_8 aucMacAddr[],
    IN BOOLEAN fgIsDeviceAddr,
    IN BOOLEAN fgAddIfNoFound
    )
{

    P_P2P_DEVICE_DESC_T prTargetDesc = (P_P2P_DEVICE_DESC_T)NULL;
    P_LINK_ENTRY_T prLinkEntry = (P_LINK_ENTRY_T)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) &&
                                        (prBssDesc != NULL) &&
                                        (aucMacAddr != NULL));

        LINK_FOR_EACH(prLinkEntry, &prBssDesc->rP2pDeviceList) {
            prTargetDesc = LINK_ENTRY(prLinkEntry, P2P_DEVICE_DESC_T, rLinkEntry);

            if (fgIsDeviceAddr) {
                if (EQUAL_MAC_ADDR(prTargetDesc->aucDeviceAddr, aucMacAddr)) {
                    break;
                }
            }
            else {
                if (EQUAL_MAC_ADDR(prTargetDesc->aucInterfaceAddr, aucMacAddr)) {
                    break;
                }
            }

            prTargetDesc = NULL;
        }

        if ((fgAddIfNoFound) && (prTargetDesc == NULL)) {
            /* Target Not Found. */
            // TODO: Use memory pool in the future.
            prTargetDesc = kalMemAlloc(sizeof(P2P_DEVICE_DESC_T), VIR_MEM_TYPE);

            if (prTargetDesc) {
                kalMemZero(prTargetDesc, sizeof(P2P_DEVICE_DESC_T));
                LINK_ENTRY_INITIALIZE(&(prTargetDesc->rLinkEntry));
                COPY_MAC_ADDR(prTargetDesc->aucDeviceAddr, aucMacAddr);
                LINK_INSERT_TAIL(&prBssDesc->rP2pDeviceList, &prTargetDesc->rLinkEntry);
                prTargetDesc->fgDevInfoValid = TRUE;
            }
            else {
                ASSERT(FALSE);
            }
        }

    } while (FALSE);

    return prTargetDesc;
} /* scanFindP2pDeviceDesc */


P_P2P_DEVICE_DESC_T
scanGetP2pDeviceDesc (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc
    )
{

    P_P2P_DEVICE_DESC_T prTargetDesc = (P_P2P_DEVICE_DESC_T)NULL;

    ASSERT(prAdapter);
    ASSERT(prBssDesc);

    if (prBssDesc->prP2pDesc == NULL) {

        prTargetDesc = kalMemAlloc(sizeof(P2P_DEVICE_DESC_T), VIR_MEM_TYPE);

        if (prTargetDesc) {
            kalMemZero(prTargetDesc, sizeof(P2P_DEVICE_DESC_T));
            LINK_ENTRY_INITIALIZE(&(prTargetDesc->rLinkEntry));
            LINK_INSERT_TAIL(&prBssDesc->rP2pDeviceList, &prTargetDesc->rLinkEntry);
            prTargetDesc->fgDevInfoValid = TRUE;
            prBssDesc->prP2pDesc = prTargetDesc;
            /* We are not sure the SrcAddr is Device Address or Interface Address. */
            COPY_MAC_ADDR(prTargetDesc->aucDeviceAddr, prBssDesc->aucSrcAddr);
            COPY_MAC_ADDR(prTargetDesc->aucInterfaceAddr, prBssDesc->aucSrcAddr);
        }
        else {

            ASSERT(FALSE);
        }
    }
    else {
        prTargetDesc = prBssDesc->prP2pDesc;
    }


    return prTargetDesc;

} /* scanFindP2pDeviceDesc */


#if 0
/*----------------------------------------------------------------------------*/
/*!
* @brief Convert the Beacon or ProbeResp Frame in SW_RFB_T to Event Packet
*
* @param[in] prSwRfb            Pointer to the receiving SW_RFB_T structure.
*
* @retval WLAN_STATUS_SUCCESS   It is a valid Scan Result and been sent to the host.
* @retval WLAN_STATUS_FAILURE   It is not a valid Scan Result.
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
scanUpdateP2pDeviceDesc (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc
    )
{
    P_P2P_DEVICE_DESC_T prP2pDesc = (P_P2P_DEVICE_DESC_T)NULL;
    P_P2P_ATTRIBUTE_T prP2pAttribute = (P_P2P_ATTRIBUTE_T)NULL;
    UINT_16 u2AttributeLen = 0;
    UINT_32 u4Idx = 0;
    BOOLEAN fgUpdateDevInfo = FALSE;

    P_DEVICE_NAME_TLV_T prP2pDevName = (P_DEVICE_NAME_TLV_T)NULL;
    P_P2P_ATTRI_GROUP_INFO_T prP2pAttriGroupInfo = (P_P2P_ATTRI_GROUP_INFO_T)NULL;

    ASSERT(prAdapter);

    prP2pDesc = scanGetP2pDeviceDesc(prAdapter, prBssDesc);

    if (!prP2pDesc) {
        ASSERT(FALSE);
        return fgUpdateDevInfo;
    }

    p2pGetP2PAttriList(prAdapter, prBssDesc->aucIEBuf, prBssDesc->u2IELength, (PPUINT_8)&prP2pAttribute, &u2AttributeLen);

    while (u2AttributeLen >= P2P_ATTRI_HDR_LEN) {
        switch (prP2pAttribute->ucId) {
            case P2P_ATTRI_ID_P2P_CAPABILITY: /* Beacon, Probe Response */
                {
                    P_P2P_ATTRI_CAPABILITY_T prP2pAttriCapability = (P_P2P_ATTRI_CAPABILITY_T)NULL;

                    prP2pAttriCapability = (P_P2P_ATTRI_CAPABILITY_T)prP2pAttribute;
                    ASSERT(prP2pAttriCapability->u2Length == 2);

                    prP2pDesc->ucDeviceCapabilityBitmap = prP2pAttriCapability->ucDeviceCap;
                    prP2pDesc->ucGroupCapabilityBitmap = prP2pAttriCapability->ucGroupCap;
                }
                break;
            case P2P_ATTRI_ID_P2P_DEV_ID:  /* Beacon */
                {
                    P_P2P_ATTRI_DEV_ID_T prP2pAttriDevID = (P_P2P_ATTRI_DEV_ID_T)NULL;

                    prP2pAttriDevID = (P_P2P_ATTRI_DEV_ID_T)prP2pAttribute;
                    ASSERT(prP2pAttriDevID->u2Length == P2P_ATTRI_MAX_LEN_P2P_DEV_ID);

                    kalMemCopy(prP2pDesc->aucDeviceAddr, prP2pAttriDevID->aucDevAddr, MAC_ADDR_LEN);
                }
                break;
            case P2P_ATTRI_ID_P2P_DEV_INFO:  /* Probe Response */
                {
                    P_P2P_ATTRI_DEV_INFO_T prP2pAttriDevInfo = (P_P2P_ATTRI_DEV_INFO_T)NULL;
                    P_P2P_DEVICE_TYPE_T prP2pDevType = (P_P2P_DEVICE_TYPE_T)NULL;
                    UINT_16 u2NameLen = 0, u2Id = 0;

                    fgUpdateDevInfo = TRUE;

                    prP2pAttriDevInfo = (P_P2P_ATTRI_DEV_INFO_T)prP2pAttribute;

                    kalMemCopy(prP2pDesc->aucDeviceAddr, prP2pAttriDevInfo->aucDevAddr, MAC_ADDR_LEN);

                    WLAN_GET_FIELD_BE16(&prP2pAttriDevInfo->u2ConfigMethodsBE, &prP2pDesc->u2ConfigMethod);

                    prP2pDevType = &prP2pDesc->rPriDevType;
                    WLAN_GET_FIELD_BE16(&prP2pAttriDevInfo->rPrimaryDevTypeBE.u2CategoryId, &prP2pDevType->u2CategoryID);
                    WLAN_GET_FIELD_BE16(&prP2pAttriDevInfo->rPrimaryDevTypeBE.u2SubCategoryId, &prP2pDevType->u2SubCategoryID);

                    ASSERT(prP2pAttriDevInfo->ucNumOfSecondaryDevType <= P2P_GC_MAX_CACHED_SEC_DEV_TYPE_COUNT);  // TODO: Fixme if secondary device type is more than 2.
                    prP2pDesc->ucSecDevTypeNum = 0;
                    for (u4Idx = 0; u4Idx < prP2pAttriDevInfo->ucNumOfSecondaryDevType; u4Idx++) {
                        if (u4Idx < P2P_GC_MAX_CACHED_SEC_DEV_TYPE_COUNT) {
                            prP2pDevType = &(prP2pDesc->arSecDevType[u4Idx]);
                            WLAN_GET_FIELD_BE16(&prP2pAttriDevInfo->arSecondaryDevTypeListBE[u4Idx].u2CategoryId, &prP2pDevType->u2CategoryID);
                            WLAN_GET_FIELD_BE16(&prP2pAttriDevInfo->arSecondaryDevTypeListBE[u4Idx].u2SubCategoryId, &prP2pDevType->u2SubCategoryID);
                            prP2pDesc->ucSecDevTypeNum++;
                        }

                    }
                    prP2pDevName = (P_DEVICE_NAME_TLV_T)((PUINT_8)prP2pAttriDevInfo->arSecondaryDevTypeListBE + (u4Idx * sizeof(DEVICE_TYPE_T)));
                    WLAN_GET_FIELD_BE16(&prP2pDevName->u2Length, &u2NameLen);
                    WLAN_GET_FIELD_BE16(&prP2pDevName->u2Id, &u2Id);
                    ASSERT(u2Id == WPS_ATTRI_ID_DEVICE_NAME);
                    if (u2NameLen > WPS_ATTRI_MAX_LEN_DEVICE_NAME) {
                        u2NameLen = WPS_ATTRI_MAX_LEN_DEVICE_NAME;
                    }
                    prP2pDesc->u2NameLength = u2NameLen;
                    kalMemCopy(prP2pDesc->aucName, prP2pDevName->aucName, prP2pDesc->u2NameLength);
                }
                break;
            case P2P_ATTRI_ID_P2P_GROUP_INFO:  /* Probe Response */
                prP2pAttriGroupInfo = (P_P2P_ATTRI_GROUP_INFO_T)prP2pAttribute;
                break;
            case P2P_ATTRI_ID_NOTICE_OF_ABSENCE:
                break;
            case P2P_ATTRI_ID_EXT_LISTEN_TIMING:
                // TODO: Not implement yet.
                //ASSERT(FALSE);
                break;
            default:
                break;
        }

        u2AttributeLen -= (prP2pAttribute->u2Length + P2P_ATTRI_HDR_LEN);

        prP2pAttribute = (P_P2P_ATTRIBUTE_T)((UINT_32)prP2pAttribute + (prP2pAttribute->u2Length + P2P_ATTRI_HDR_LEN));

    }


    if (prP2pAttriGroupInfo != NULL) {
        P_P2P_CLIENT_INFO_DESC_T prClientInfoDesc = (P_P2P_CLIENT_INFO_DESC_T)NULL;
        P_P2P_DEVICE_TYPE_T prP2pDevType = (P_P2P_DEVICE_TYPE_T)NULL;

        scanInvalidAllP2pClientDevice(prAdapter, prBssDesc);

        /* GO/Device itself. */
        prP2pDesc->fgDevInfoValid = TRUE;

        prClientInfoDesc = (P_P2P_CLIENT_INFO_DESC_T)prP2pAttriGroupInfo->arClientDesc;
        u2AttributeLen = prP2pAttriGroupInfo->u2Length;


        while (u2AttributeLen > 0) {
            prP2pDesc = scanFindP2pDeviceDesc(prAdapter, prBssDesc, prClientInfoDesc->aucDevAddr, TRUE, TRUE);

            if (!prP2pDesc) {
                ASSERT(FALSE);
                break; /* while */
            }

            prP2pDesc->fgDevInfoValid = TRUE;

            /* Basic size for P2P client info descriptor. */
            ASSERT(u2AttributeLen >= 25);
            if (u2AttributeLen < 25) {
                DBGLOG(P2P, WARN, ("Length incorrect warning.\n"));
                break;
            }
            COPY_MAC_ADDR(prP2pDesc->aucInterfaceAddr, prClientInfoDesc->aucIfAddr);

            prP2pDesc->ucDeviceCapabilityBitmap = prClientInfoDesc->ucDeviceCap;

            WLAN_GET_FIELD_BE16(&prClientInfoDesc->u2ConfigMethodsBE, &prP2pDesc->u2ConfigMethod);

            prP2pDevType = &(prP2pDesc->rPriDevType);
            WLAN_GET_FIELD_BE16(&prClientInfoDesc->rPrimaryDevTypeBE.u2CategoryId, &prP2pDevType->u2CategoryID);
            WLAN_GET_FIELD_BE16(&prClientInfoDesc->rPrimaryDevTypeBE.u2SubCategoryId, &prP2pDevType->u2SubCategoryID);

            ASSERT(prClientInfoDesc->ucNumOfSecondaryDevType <= P2P_GC_MAX_CACHED_SEC_DEV_TYPE_COUNT);
            prP2pDesc->ucSecDevTypeNum = 0;
            for (u4Idx = 0; u4Idx < prClientInfoDesc->ucNumOfSecondaryDevType; u4Idx++) {
                if (u4Idx < P2P_GC_MAX_CACHED_SEC_DEV_TYPE_COUNT) {
                    prP2pDevType = &(prP2pDesc->arSecDevType[u4Idx]);
                    WLAN_GET_FIELD_BE16(&prClientInfoDesc->arSecondaryDevTypeListBE[u4Idx].u2CategoryId, &prP2pDevType->u2CategoryID);
                    WLAN_GET_FIELD_BE16(&prClientInfoDesc->arSecondaryDevTypeListBE[u4Idx].u2SubCategoryId, &prP2pDevType->u2SubCategoryID);
                    prP2pDesc->ucSecDevTypeNum++;
                }

            }
            prP2pDevName = (P_DEVICE_NAME_TLV_T)(prClientInfoDesc->arSecondaryDevTypeListBE + (u4Idx * sizeof(DEVICE_TYPE_T)));
            WLAN_GET_FIELD_BE16(&prP2pDevName->u2Length, &prP2pDesc->u2NameLength);
            if (prP2pDesc->u2NameLength > WPS_ATTRI_MAX_LEN_DEVICE_NAME) {
                prP2pDesc->u2NameLength = WPS_ATTRI_MAX_LEN_DEVICE_NAME;
            }

            kalMemCopy(prP2pDesc->aucName, prP2pDevName->aucName, prP2pDesc->u2NameLength);

            u2AttributeLen -= (prClientInfoDesc->ucLength + P2P_CLIENT_INFO_DESC_HDR_LEN);
            prClientInfoDesc = (P_P2P_CLIENT_INFO_DESC_T)((UINT_32)prClientInfoDesc + (UINT_32)prClientInfoDesc->ucLength + P2P_CLIENT_INFO_DESC_HDR_LEN);
        }

        scanRemoveInvalidP2pClientDevice(prAdapter, prBssDesc);
    }

    return fgUpdateDevInfo;
} /* end of scanAddP2pDeviceInfo() */

#endif



/*----------------------------------------------------------------------------*/
/*!
* @brief Convert the Beacon or ProbeResp Frame in SW_RFB_T to Event Packet
*
* @param[in] prSwRfb            Pointer to the receiving SW_RFB_T structure.
*
* @retval WLAN_STATUS_SUCCESS   It is a valid Scan Result and been sent to the host.
* @retval WLAN_STATUS_FAILURE   It is not a valid Scan Result.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
scanSendDeviceDiscoverEvent (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc,
    IN P_SW_RFB_T prSwRfb
    )
{
    EVENT_P2P_DEV_DISCOVER_RESULT_T rEventDevInfo;
#if 1
    P_LINK_ENTRY_T prLinkEntry = (P_LINK_ENTRY_T)NULL;
    P_P2P_DEVICE_DESC_T prTargetDesc = (P_P2P_DEVICE_DESC_T)NULL;

    LINK_FOR_EACH(prLinkEntry, &prBssDesc->rP2pDeviceList) {
        prTargetDesc = LINK_ENTRY(prLinkEntry, P2P_DEVICE_DESC_T, rLinkEntry);

        COPY_MAC_ADDR(rEventDevInfo.aucDeviceAddr, prTargetDesc->aucDeviceAddr);
        COPY_MAC_ADDR(rEventDevInfo.aucInterfaceAddr, prTargetDesc->aucInterfaceAddr);

        rEventDevInfo.ucDeviceCapabilityBitmap = prTargetDesc->ucDeviceCapabilityBitmap;
        rEventDevInfo.ucGroupCapabilityBitmap = prTargetDesc->ucGroupCapabilityBitmap;
        rEventDevInfo.u2ConfigMethod = prTargetDesc->u2ConfigMethod;

        kalMemCopy(&rEventDevInfo.rPriDevType,
                                &prTargetDesc->rPriDevType,
                                sizeof(P2P_DEVICE_TYPE_T));

        kalMemCopy(rEventDevInfo.arSecDevType,
                                prTargetDesc->arSecDevType,
                                (prTargetDesc->ucSecDevTypeNum * sizeof(P2P_DEVICE_TYPE_T)));

        rEventDevInfo.ucSecDevTypeNum = prTargetDesc->ucSecDevTypeNum;

        rEventDevInfo.u2NameLength = prTargetDesc->u2NameLength;
        kalMemCopy(rEventDevInfo.aucName,
                                prTargetDesc->aucName,
                                prTargetDesc->u2NameLength);

        COPY_MAC_ADDR(rEventDevInfo.aucBSSID, prBssDesc->aucBSSID);

        if (prTargetDesc == prBssDesc->prP2pDesc) {
            nicRxAddP2pDevice(prAdapter,
                &rEventDevInfo,
                prBssDesc->aucIEBuf,
                prBssDesc->u2IELength);
        }
        else {
            nicRxAddP2pDevice(prAdapter,
                &rEventDevInfo,
                NULL,
                0);
        }
    }

    kalP2PIndicateFound(prAdapter->prGlueInfo);

#else

    P_P2P_SPECIFIC_BSS_INFO_T prP2pSpecificBssInfo = (P_P2P_SPECIFIC_BSS_INFO_T)NULL;
    P_P2P_ATTRIBUTE_T prP2pAttribute = (P_P2P_ATTRIBUTE_T)NULL;
    UINT_16 u2AttributeLen = 0;
    UINT_32 u4Idx = 0;
    P_P2P_ATTRI_GROUP_INFO_T prP2pAttriGroupInfo = (P_P2P_ATTRI_GROUP_INFO_T)NULL;
    P_DEVICE_NAME_TLV_T prP2pDevName = (P_DEVICE_NAME_TLV_T)NULL;

    ASSERT(prAdapter);

    prP2pSpecificBssInfo = &prAdapter->rWifiVar.rP2pSpecificBssInfo;

#if 1
    p2pGetP2PAttriList(prAdapter, prBssDesc->aucIEBuf, prBssDesc->u2IELength, (PPUINT_8)&prP2pAttribute, &u2AttributeLen);
#else
    prP2pAttribute = (P_P2P_ATTRIBUTE_T)&prP2pSpecificBssInfo->aucAttributesCache[0];
    u2AttributeLen = prP2pSpecificBssInfo->u2AttributeLen;
#endif
    rEventDevInfo.fgDevInfoValid = FALSE;

    while (u2AttributeLen >= P2P_ATTRI_HDR_LEN) {
        switch (prP2pAttribute->ucId) {
            case P2P_ATTRI_ID_P2P_CAPABILITY:
                {
                    P_P2P_ATTRI_CAPABILITY_T prP2pAttriCapability = (P_P2P_ATTRI_CAPABILITY_T)NULL;

                    prP2pAttriCapability = (P_P2P_ATTRI_CAPABILITY_T)prP2pAttribute;
                    ASSERT(prP2pAttriCapability->u2Length == 2);
                    rEventDevInfo.ucDeviceCapabilityBitmap = prP2pAttriCapability->ucDeviceCap;
                    rEventDevInfo.ucGroupCapabilityBitmap = prP2pAttriCapability->ucGroupCap;
                }
                break;
            case P2P_ATTRI_ID_P2P_DEV_ID:
                {
                    P_P2P_ATTRI_DEV_ID_T prP2pAttriDevID = (P_P2P_ATTRI_DEV_ID_T)NULL;

                    prP2pAttriDevID = (P_P2P_ATTRI_DEV_ID_T)prP2pAttribute;
                    ASSERT(prP2pAttriDevID->u2Length == 6);
                    kalMemCopy(rEventDevInfo.aucCommunicateAddr, prP2pAttriDevID->aucDevAddr, MAC_ADDR_LEN);
                }
                break;
            case P2P_ATTRI_ID_P2P_DEV_INFO:
                {
                    P_P2P_ATTRI_DEV_INFO_T prP2pAttriDevInfo = (P_P2P_ATTRI_DEV_INFO_T)NULL;
                    P_P2P_DEVICE_TYPE_T prP2pDevType = (P_P2P_DEVICE_TYPE_T)NULL;

                    prP2pAttriDevInfo = (P_P2P_ATTRI_DEV_INFO_T)prP2pAttribute;
                    rEventDevInfo.fgDevInfoValid = TRUE;
                    kalMemCopy(rEventDevInfo.aucCommunicateAddr, prP2pAttriDevInfo->aucDevAddr, MAC_ADDR_LEN);
                    rEventDevInfo.u2ConfigMethod = prP2pAttriDevInfo->u2ConfigMethodsBE;

                    prP2pDevType = &rEventDevInfo.rPriDevType;
                    prP2pDevType->u2CategoryID = prP2pAttriDevInfo->rPrimaryDevTypeBE.u2CategoryId;
                    prP2pDevType->u2SubCategoryID = prP2pAttriDevInfo->rPrimaryDevTypeBE.u2SubCategoryId;

                    ASSERT(prP2pAttriDevInfo->ucNumOfSecondaryDevType <= 2);  // TODO: Fixme if secondary device type is more than 2.
                    for (u4Idx = 0; u4Idx < prP2pAttriDevInfo->ucNumOfSecondaryDevType; u4Idx++) {
                        // TODO: Current sub device type can only support 2.
                        prP2pDevType = &rEventDevInfo.arSecDevType[u4Idx];
                        prP2pDevType->u2CategoryID = prP2pAttriDevInfo->rPrimaryDevTypeBE.u2CategoryId;
                        prP2pDevType->u2SubCategoryID = prP2pAttriDevInfo->rPrimaryDevTypeBE.u2SubCategoryId;
                    }

                    prP2pDevName = (P_DEVICE_NAME_TLV_T)(prP2pAttriDevInfo->arSecondaryDevTypeListBE + (u4Idx * sizeof(DEVICE_TYPE_T)));
                    ASSERT(prP2pDevName->u2Id == 0x1011);
                    ASSERT(prP2pDevName->u2Length <= 32);   // TODO: Fixme if device name length is longer than 32 bytes.
                    kalMemCopy(rEventDevInfo.aucName, prP2pDevName->aucName, prP2pDevName->u2Length);
                }
                break;
            case P2P_ATTRI_ID_P2P_GROUP_INFO:
                prP2pAttriGroupInfo = (P_P2P_ATTRI_GROUP_INFO_T)prP2pAttribute;
                break;
        }

        u2AttributeLen -= (prP2pAttribute->u2Length + P2P_ATTRI_HDR_LEN);

        prP2pAttribute = (P_P2P_ATTRIBUTE_T)((UINT_32)prP2pAttribute + (prP2pAttribute->u2Length + P2P_ATTRI_HDR_LEN));

    }

    nicRxAddP2pDevice(prAdapter,
            &rEventDevInfo);

    if (prP2pAttriGroupInfo != NULL) {
        P_P2P_CLIENT_INFO_DESC_T prClientInfoDesc = (P_P2P_CLIENT_INFO_DESC_T)NULL;
        P_P2P_DEVICE_TYPE_T prP2pDevType = (P_P2P_DEVICE_TYPE_T)NULL;

        prClientInfoDesc = prP2pAttriGroupInfo->arClientDesc;
        u2AttributeLen = prP2pAttriGroupInfo->u2Length;

        while (u2AttributeLen > 0) {
            /* Basic size for P2P client info descriptor. */
            ASSERT(u2AttributeLen >= 25);
            rEventDevInfo.fgDevInfoValid = TRUE;
            kalMemCopy(rEventDevInfo.aucCommunicateAddr, prClientInfoDesc->aucIfAddr, MAC_ADDR_LEN);
            rEventDevInfo.ucDeviceCapabilityBitmap = prClientInfoDesc->ucDeviceCap;
            rEventDevInfo.u2ConfigMethod = prClientInfoDesc->u2ConfigMethodsBE;

            prP2pDevType = &rEventDevInfo.rPriDevType;
            prP2pDevType->u2CategoryID = prClientInfoDesc->rPrimaryDevTypeBE.u2CategoryId;
            prP2pDevType->u2SubCategoryID = prClientInfoDesc->rPrimaryDevTypeBE.u2SubCategoryId;

            ASSERT(prClientInfoDesc->ucNumOfSecondaryDevType <= 2);  // TODO: Fixme if secondary device type is more than 2.
            for (u4Idx = 0; u4Idx < prClientInfoDesc->ucNumOfSecondaryDevType; u4Idx++) {
                // TODO: Current sub device type can only support 2.
                prP2pDevType = &rEventDevInfo.arSecDevType[u4Idx];
                prP2pDevType->u2CategoryID = prClientInfoDesc->arSecondaryDevTypeListBE[u4Idx].u2CategoryId;
                prP2pDevType->u2SubCategoryID = prClientInfoDesc->arSecondaryDevTypeListBE[u4Idx].u2SubCategoryId;
            }

            prP2pDevName = (P_DEVICE_NAME_TLV_T)(prClientInfoDesc->arSecondaryDevTypeListBE + (u4Idx * sizeof(DEVICE_TYPE_T)));
            ASSERT(prP2pDevName->u2Id == 0x1011);
            ASSERT(prP2pDevName->u2Length <= 32);   // TODO: Fixme if device name length is longer than 32 bytes.
            kalMemCopy(&rEventDevInfo.aucName, prP2pDevName->aucName, prP2pDevName->u2Length);

            nicRxAddP2pDevice(prAdapter,
                &rEventDevInfo);

            u2AttributeLen -= prP2pAttriGroupInfo->u2Length;
            prP2pAttriGroupInfo = prP2pAttriGroupInfo + prP2pAttriGroupInfo->u2Length + 1;
        }

    }
#endif
    return WLAN_STATUS_SUCCESS;
} /* scanSendDeviceDiscoverEvent */

VOID
scanP2pProcessBeaconAndProbeResp(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    IN P_WLAN_STATUS prStatus,
    IN P_BSS_DESC_T prBssDesc,
    IN P_WLAN_BEACON_FRAME_T prWlanBeaconFrame
    )
{
    P_BSS_INFO_T prP2pBssInfo = (P_BSS_INFO_T)NULL;
    P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = (P_P2P_CONNECTION_SETTINGS_T)NULL;
    prP2pBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_P2P_INDEX]);
    prP2pConnSettings = prAdapter->rWifiVar.prP2PConnSettings;

    if (prBssDesc->fgIsP2PPresent) {

        if ((!prP2pBssInfo->ucDTIMPeriod) &&          // First time.
            (prP2pBssInfo->eCurrentOPMode == OP_MODE_INFRASTRUCTURE) &&     // P2P GC
                (prP2pBssInfo->eConnectionState == PARAM_MEDIA_STATE_CONNECTED) &&   // Connected
                ((prWlanBeaconFrame->u2FrameCtrl & MASK_FRAME_TYPE) == MAC_FRAME_BEACON) &&  // TX Beacon
                EQUAL_SSID(prBssDesc->aucSSID,                                                                  // SSID Match
                            prBssDesc->ucSSIDLen,
                            prP2pConnSettings->aucSSID,
                            prP2pConnSettings->ucSSIDLen)) {


            prP2pBssInfo->ucDTIMPeriod = prBssDesc->ucDTIMPeriod;
            nicPmIndicateBssConnected(prAdapter, NETWORK_TYPE_P2P_INDEX);
        }

        do {
            RF_CHANNEL_INFO_T rChannelInfo;

            ASSERT_BREAK((prSwRfb != NULL) && (prBssDesc != NULL));

			if (((prWlanBeaconFrame->u2FrameCtrl & MASK_FRAME_TYPE) != MAC_FRAME_PROBE_RSP)) {
                // Only report Probe Response frame to supplicant.
                /* Probe response collect much more information. */
                break;
            }

            rChannelInfo.ucChannelNum = prBssDesc->ucChannelNum;
            rChannelInfo.eBand = prBssDesc->eBand;

            kalP2PIndicateBssInfo(prAdapter->prGlueInfo,
                            (PUINT_8)prSwRfb->pvHeader,
                            (UINT_32)prSwRfb->u2PacketLen,
                            &rChannelInfo,
                            RCPI_TO_dBm(prBssDesc->ucRCPI));


        } while (FALSE);
    }
}

VOID
scnEventReturnChannel (
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucScnSeqNum
    )
{

    CMD_SCAN_CANCEL rCmdScanCancel;

    /* send cancel message to firmware domain */
    rCmdScanCancel.ucSeqNum = ucScnSeqNum;
    rCmdScanCancel.ucIsExtChannel = (UINT_8) FALSE;

    wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SCAN_CANCEL,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            sizeof(CMD_SCAN_CANCEL),
            (PUINT_8)&rCmdScanCancel,
            NULL,
            0);

    return;
} /* scnEventReturnChannel */

VOID
scanRemoveAllP2pBssDesc(
    IN P_ADAPTER_T prAdapter
    )
{
    P_LINK_T prBSSDescList;
    P_BSS_DESC_T prBssDesc;
    P_BSS_DESC_T prBSSDescNext;

    ASSERT(prAdapter);

    prBSSDescList = &(prAdapter->rWifiVar.rScanInfo.rBSSDescList);

    /* Search BSS Desc from current SCAN result list. */
    LINK_FOR_EACH_ENTRY_SAFE(prBssDesc, prBSSDescNext, prBSSDescList, rLinkEntry, BSS_DESC_T) {
        scanRemoveP2pBssDesc(prAdapter, prBssDesc);
    }
} /* scanRemoveAllP2pBssDesc */

VOID
scanRemoveP2pBssDesc(
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc
    )
{

    return;
} /* scanRemoveP2pBssDesc */


P_BSS_DESC_T
scanP2pSearchDesc (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN P_P2P_CONNECTION_REQ_INFO_T prConnReqInfo
    )
{
    P_BSS_DESC_T prCandidateBssDesc = (P_BSS_DESC_T)NULL, prBssDesc = (P_BSS_DESC_T)NULL;
    P_LINK_T prBssDescList = (P_LINK_T)NULL;

    do {
        if ((prAdapter == NULL) ||
                (prP2pBssInfo == NULL) ||
                (prConnReqInfo == NULL)) {
            break;
        }


        prBssDescList = &(prAdapter->rWifiVar.rScanInfo.rBSSDescList);

        DBGLOG(P2P, LOUD, ("Connecting to BSSID: "MACSTR"\n", MAC2STR(prConnReqInfo->aucBssid)));
        DBGLOG(P2P, LOUD, ("Connecting to SSID:%s, length:%d\n",
                            prConnReqInfo->rSsidStruct.aucSsid,
                            prConnReqInfo->rSsidStruct.ucSsidLen));

        LINK_FOR_EACH_ENTRY(prBssDesc, prBssDescList, rLinkEntry, BSS_DESC_T) {
            DBGLOG(P2P, LOUD, ("Checking BSS: "MACSTR"\n", MAC2STR(prBssDesc->aucBSSID)));

            if (prBssDesc->eBSSType != BSS_TYPE_INFRASTRUCTURE) {
                DBGLOG(P2P, LOUD, ("Ignore mismatch BSS type.\n"));
                continue;
            }


            if (UNEQUAL_MAC_ADDR(prBssDesc->aucBSSID, prConnReqInfo->aucBssid)) {
                DBGLOG(P2P, LOUD, ("Ignore mismatch BSSID.\n"));
                continue;
            }


            /* SSID should be the same? SSID is vary for each connection. so... */
            if (UNEQUAL_SSID(prConnReqInfo->rSsidStruct.aucSsid,
                                                prConnReqInfo->rSsidStruct.ucSsidLen,
                                                prBssDesc->aucSSID,
                                                prBssDesc->ucSSIDLen)) {

                DBGLOG(P2P, TRACE, ("Connecting to BSSID: "MACSTR"\n", MAC2STR(prConnReqInfo->aucBssid)));
                DBGLOG(P2P, TRACE, ("Connecting to SSID:%s, length:%d\n",
                                    prConnReqInfo->rSsidStruct.aucSsid,
                                    prConnReqInfo->rSsidStruct.ucSsidLen));
                DBGLOG(P2P, TRACE, ("Checking SSID:%s, length:%d\n",
                                                prBssDesc->aucSSID,
                                                prBssDesc->ucSSIDLen));
                DBGLOG(P2P, TRACE, ("Ignore mismatch SSID, (But BSSID match).\n"));
                ASSERT(FALSE);
                continue;
            }

            if (!prBssDesc->fgIsP2PPresent) {
                DBGLOG(P2P, ERROR, ("SSID, BSSID, BSSTYPE match, but no P2P IE present.\n"));
                continue;
            }


            /* Final decision. */
            prCandidateBssDesc = prBssDesc;
            break;
        }



    } while (FALSE);

    return prCandidateBssDesc;
} /* scanP2pSearchDesc */




