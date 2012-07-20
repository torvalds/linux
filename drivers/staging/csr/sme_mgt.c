/*
 * ---------------------------------------------------------------------------
 * FILE:     sme_mgt.c
 *
 * PURPOSE:
 *      This file contains the driver specific implementation of
 *      the SME MGT SAP.
 *      It is part of the porting exercise.
 *
 * Copyright (C) 2008-2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */

#include "csr_wifi_hip_unifiversion.h"
#include "unifi_priv.h"
#include "csr_wifi_hip_conversions.h"
/*
 * This file implements the SME MGT API. It contains the following functions:
 * CsrWifiSmeWifiFlightmodeCfmSend()
 * CsrWifiSmeWifiOnCfmSend()
 * CsrWifiSmeWifiOffCfmSend()
 * CsrWifiSmeWifiOffIndSend()
 * CsrWifiSmeScanFullCfmSend()
 * CsrWifiSmeScanResultsGetCfmSend()
 * CsrWifiSmeScanResultIndSend()
 * CsrWifiSmeScanResultsFlushCfmSend()
 * CsrWifiSmeConnectCfmSend()
 * CsrWifiSmeMediaStatusIndSend()
 * CsrWifiSmeDisconnectCfmSend()
 * CsrWifiSmeKeyCfmSend()
 * CsrWifiSmeMulticastAddressCfmSend()
 * CsrWifiSmeSetValueCfmSend()
 * CsrWifiSmeGetValueCfmSend()
 * CsrWifiSmeMicFailureIndSend()
 * CsrWifiSmePmkidCfmSend()
 * CsrWifiSmePmkidCandidateListIndSend()
 * CsrWifiSmeMibSetCfmSend()
 * CsrWifiSmeMibGetCfmSend()
 * CsrWifiSmeMibGetNextCfmSend()
 * CsrWifiSmeConnectionQualityIndSend()
 * CsrWifiSmePacketFilterSetCfmSend()
 * CsrWifiSmeTspecCfmSend()
 * CsrWifiSmeTspecIndSend()
 * CsrWifiSmeBlacklistCfmSend()
 * CsrWifiSmeEventMaskSetCfmSend()
 * CsrWifiSmeRoamStartIndSend()
 * CsrWifiSmeRoamCompleteIndSend()
 * CsrWifiSmeAssociationStartIndSend()
 * CsrWifiSmeAssociationCompleteIndSend()
 * CsrWifiSmeIbssStationIndSend()
 */


void CsrWifiSmeMicFailureIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeMicFailureInd* ind = (CsrWifiSmeMicFailureInd*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeMicFailureIndSend: invalid priv\n");
        return;
    }

    unifi_trace(priv, UDBG1,
                "CsrWifiSmeMicFailureIndSend: count=%d, KeyType=%d\n",
                ind->count, ind->keyType);

    wext_send_michaelmicfailure_event(priv, ind->count, ind->address, ind->keyType, ind->interfaceTag);
#endif
}


void CsrWifiSmePmkidCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmePmkidCfm* cfm = (CsrWifiSmePmkidCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmePmkidCfmSend: Invalid ospriv.\n");
        return;
    }

    /*
     * WEXT never does a GET operation the PMKIDs, so we don't need
     * handle data returned in pmkids.
     */

    sme_complete_request(priv, cfm->status);
#endif
}


void CsrWifiSmePmkidCandidateListIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmePmkidCandidateListInd* ind = (CsrWifiSmePmkidCandidateListInd*)msg;
    int i;

    if (priv->smepriv == NULL) {
        unifi_error(priv, "CsrWifiSmePmkidCandidateListIndSend: invalid smepriv\n");
        return;
    }

    for (i = 0; i < ind->pmkidCandidatesCount; i++)
    {
        wext_send_pmkid_candidate_event(priv, ind->pmkidCandidates[i].bssid, ind->pmkidCandidates[i].preAuthAllowed, ind->interfaceTag);
    }
#endif
}

void CsrWifiSmeScanResultsFlushCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeScanResultsGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeScanResultsGetCfm* cfm = (CsrWifiSmeScanResultsGetCfm*)msg;
    int bytesRequired = cfm->scanResultsCount * sizeof(CsrWifiSmeScanResult);
    int i;
    u8* current_buff;
    CsrWifiSmeScanResult* scanCopy;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeScanResultsGetCfmSend: Invalid ospriv.\n");
        return;
    }

    /* Calc the size of the buffer reuired */
    for (i = 0; i < cfm->scanResultsCount; ++i) {
        const CsrWifiSmeScanResult *scan_result = &cfm->scanResults[i];
        bytesRequired += scan_result->informationElementsLength;
    }

    /* Take a Copy of the scan Results :-) */
    scanCopy = CsrPmemAlloc(bytesRequired);
    memcpy(scanCopy, cfm->scanResults, sizeof(CsrWifiSmeScanResult) * cfm->scanResultsCount);

    /* Take a Copy of the Info Elements AND update the scan result pointers */
    current_buff = (u8*)&scanCopy[cfm->scanResultsCount];
    for (i = 0; i < cfm->scanResultsCount; ++i)
    {
        CsrWifiSmeScanResult *scan_result = &scanCopy[i];
        CsrMemCpy(current_buff, scan_result->informationElements, scan_result->informationElementsLength);
        scan_result->informationElements = current_buff;
        current_buff += scan_result->informationElementsLength;
    }

    priv->sme_reply.reply_scan_results_count = cfm->scanResultsCount;
    priv->sme_reply.reply_scan_results = scanCopy;

    sme_complete_request(priv, cfm->status);
#endif
}


void CsrWifiSmeScanFullCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeScanFullCfm* cfm = (CsrWifiSmeScanFullCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeScanFullCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}


void CsrWifiSmeScanResultIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{

}


void CsrWifiSmeConnectCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeConnectCfm* cfm = (CsrWifiSmeConnectCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeConnectCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}


void CsrWifiSmeDisconnectCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeDisconnectCfm* cfm = (CsrWifiSmeDisconnectCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeDisconnectCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}


void CsrWifiSmeKeyCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeKeyCfm* cfm = (CsrWifiSmeKeyCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeKeyCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}


void CsrWifiSmeMulticastAddressCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeMulticastAddressCfm* cfm = (CsrWifiSmeMulticastAddressCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeMulticastAddressCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeWifiFlightmodeCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeWifiFlightmodeCfm* cfm = (CsrWifiSmeWifiFlightmodeCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeWifiFlightmodeCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeWifiOnCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeWifiOnCfm* cfm = (CsrWifiSmeWifiOnCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeWifiOnCfmSend: Invalid ospriv.\n");
        return;
    }

    unifi_trace(priv, UDBG4,
                "CsrWifiSmeWifiOnCfmSend: wake up status %d\n", cfm->status);
#ifdef CSR_SUPPORT_WEXT_AP
    sme_complete_request(priv, cfm->status);
#endif

#endif
}

void CsrWifiSmeWifiOffCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeWifiOffCfm* cfm = (CsrWifiSmeWifiOffCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeWifiOffCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}


void CsrWifiSmeWifiOffIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeWifiOffInd* ind = (CsrWifiSmeWifiOffInd*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiRouterCtrlStoppedReqSend: Invalid ospriv.\n");
        return;
    }

    if (priv->smepriv == NULL) {
        unifi_error(priv, "CsrWifiRouterCtrlStoppedReqSend: invalid smepriv\n");
        return;
    }

    /*
     * If the status indicates an error, the SME is in a stopped state.
     * We need to start it again in order to reinitialise UniFi.
     */
    switch (ind->reason) {
        case CSR_WIFI_SME_CONTROL_INDICATION_ERROR:
          unifi_trace(priv, UDBG1,
                      "CsrWifiRouterCtrlStoppedReqSend: Restarting SME (ind:%d)\n",
                      ind->reason);

          /* On error, restart the SME */
          sme_mgt_wifi_on(priv);
          break;
        case CSR_WIFI_SME_CONTROL_INDICATION_EXIT:
#ifdef CSR_SUPPORT_WEXT_AP
          sme_complete_request(priv, 0);
#endif
          break;
        default:
          break;
    }

#endif
}

void CsrWifiSmeVersionsGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeVersionsGetCfm* cfm = (CsrWifiSmeVersionsGetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeVersionsGetCfmSend: Invalid ospriv.\n");
        return;
    }

    priv->sme_reply.versions = cfm->versions;
    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmePowerConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmePowerConfigGetCfm* cfm = (CsrWifiSmePowerConfigGetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmePowerConfigGetCfmSend: Invalid ospriv.\n");
        return;
    }

    priv->sme_reply.powerConfig = cfm->powerConfig;
    sme_complete_request(priv, cfm->status);
}

void CsrWifiSmeHostConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeHostConfigGetCfm* cfm = (CsrWifiSmeHostConfigGetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeHostConfigGetCfmSend: Invalid ospriv.\n");
        return;
    }

    priv->sme_reply.hostConfig = cfm->hostConfig;
    sme_complete_request(priv, cfm->status);
}

void CsrWifiSmeCoexInfoGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeCoexInfoGetCfm* cfm = (CsrWifiSmeCoexInfoGetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeCoexInfoGetCfmSend: Invalid ospriv.\n");
        return;
    }

    priv->sme_reply.coexInfo = cfm->coexInfo;
    sme_complete_request(priv, cfm->status);
}

void CsrWifiSmeCoexConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeCoexConfigGetCfm* cfm = (CsrWifiSmeCoexConfigGetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeCoexConfigGetCfmSend: Invalid ospriv.\n");
        return;
    }

    priv->sme_reply.coexConfig = cfm->coexConfig;
    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeMibConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeMibConfigGetCfm* cfm = (CsrWifiSmeMibConfigGetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeMibConfigGetCfmSend: Invalid ospriv.\n");
        return;
    }

    priv->sme_reply.mibConfig = cfm->mibConfig;
    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeConnectionInfoGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeConnectionInfoGetCfm* cfm = (CsrWifiSmeConnectionInfoGetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeConnectionInfoGetCfmSend: Invalid ospriv.\n");
        return;
    }

    priv->sme_reply.connectionInfo = cfm->connectionInfo;
    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeConnectionConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeConnectionConfigGetCfm* cfm = (CsrWifiSmeConnectionConfigGetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeConnectionConfigGetCfmSend: Invalid ospriv.\n");
        return;
    }

    priv->sme_reply.connectionConfig = cfm->connectionConfig;
    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeConnectionStatsGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeConnectionStatsGetCfm* cfm = (CsrWifiSmeConnectionStatsGetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeConnectionStatsGetCfmSend: Invalid ospriv.\n");
        return;
    }

    priv->sme_reply.connectionStats = cfm->connectionStats;
    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeMibSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeMibSetCfm* cfm = (CsrWifiSmeMibSetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeMibSetCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeMibGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeMibGetCfm* cfm = (CsrWifiSmeMibGetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeMibGetCfmSend: Invalid ospriv.\n");
        return;
    }

    if (cfm->mibAttribute == NULL) {
        unifi_error(priv, "CsrWifiSmeMibGetCfmSend: Empty reply.\n");
        sme_complete_request(priv, cfm->status);
        return;
    }

    if ((priv->mib_cfm_buffer != NULL) &&
        (priv->mib_cfm_buffer_length >= cfm->mibAttributeLength)) {
        memcpy(priv->mib_cfm_buffer, cfm->mibAttribute, cfm->mibAttributeLength);
        priv->mib_cfm_buffer_length = cfm->mibAttributeLength;
    } else {
        unifi_error(priv,
                    "CsrWifiSmeMibGetCfmSend: No room to store MIB data (have=%d need=%d).\n",
                    priv->mib_cfm_buffer_length, cfm->mibAttributeLength);
    }

    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeMibGetNextCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeMibGetNextCfm* cfm = (CsrWifiSmeMibGetNextCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeMibGetNextCfmSend: Invalid ospriv.\n");
        return;
    }

    /* Need to copy MIB data */
    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeConnectionQualityIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeConnectionQualityInd* ind = (CsrWifiSmeConnectionQualityInd*)msg;
    int signal, noise, snr;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeConnectionQualityIndSend: Invalid ospriv.\n");
        return;
    }

    /*
     * level and noise below are mapped into an unsigned 8 bit number,
     * ranging from [-192; 63]. The way this is achieved is simply to
     * add 0x100 onto the number if it is negative,
     * once clipped to the correct range.
     */
    signal = ind->linkQuality.unifiRssi;
    /* Clip range of snr */
    snr    = (ind->linkQuality.unifiSnr > 0) ? ind->linkQuality.unifiSnr : 0; /* In dB relative, from 0 - 255 */
    snr    = (snr < 255) ? snr : 255;
    noise  = signal - snr;

    /* Clip range of signal */
    signal = (signal < 63) ? signal : 63;
    signal = (signal > -192) ? signal : -192;

    /* Clip range of noise */
    noise = (noise < 63) ? noise : 63;
    noise = (noise > -192) ? noise : -192;

    /* Make u8 */
    signal = ( signal < 0 ) ? signal + 0x100 : signal;
    noise = ( noise < 0 ) ? noise + 0x100 : noise;

    priv->wext_wireless_stats.qual.level   = (u8)signal; /* -192 : 63 */
    priv->wext_wireless_stats.qual.noise   = (u8)noise;  /* -192 : 63 */
    priv->wext_wireless_stats.qual.qual    = snr;         /* 0 : 255 */
    priv->wext_wireless_stats.qual.updated = 0;

#if WIRELESS_EXT > 16
    priv->wext_wireless_stats.qual.updated |= IW_QUAL_LEVEL_UPDATED |
                                              IW_QUAL_NOISE_UPDATED |
                                              IW_QUAL_QUAL_UPDATED;
#if WIRELESS_EXT > 18
    priv->wext_wireless_stats.qual.updated |= IW_QUAL_DBM;
#endif
#endif
#endif
}

void CsrWifiSmePacketFilterSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmePacketFilterSetCfmSend: Invalid ospriv.\n");
        return;
    }

    /* The packet filter set request does not block for a reply */
}

void CsrWifiSmeTspecCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeTspecCfm* cfm = (CsrWifiSmeTspecCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeTspecCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
}

void CsrWifiSmeTspecIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeBlacklistCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeEventMaskSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}


void CsrWifiSmeRoamStartIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeRoamCompleteIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    /* This is called when the association completes, before any 802.1x authentication */
}

void CsrWifiSmeAssociationStartIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeAssociationCompleteIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeIbssStationIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeWifiOnIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeRestrictedAccessEnableCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeRestrictedAccessDisableCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}


void CsrWifiSmeAdhocConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeAdhocConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeAdhocConfigSetCfm* cfm = (CsrWifiSmeAdhocConfigSetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeSmeConfigSetCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeCalibrationDataGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeCalibrationDataSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeCalibrationDataSetCfm* cfm = (CsrWifiSmeCalibrationDataSetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeSmeConfigSetCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeCcxConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeCcxConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeCcxConfigSetCfm* cfm = (CsrWifiSmeCcxConfigSetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeSmeConfigSetCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeCloakedSsidsGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeCloakedSsidsSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeCloakedSsidsSetCfm* cfm = (CsrWifiSmeCloakedSsidsSetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeSmeConfigSetCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}


void CsrWifiSmeCoexConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeCoexConfigSetCfm* cfm = (CsrWifiSmeCoexConfigSetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeSmeConfigSetCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeHostConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeHostConfigSetCfm* cfm = (CsrWifiSmeHostConfigSetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeSmeConfigSetCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
}

void CsrWifiSmeLinkQualityGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}


void CsrWifiSmeMibConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeMibConfigSetCfm* cfm = (CsrWifiSmeMibConfigSetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeSmeConfigSetCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmePermanentMacAddressGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmePowerConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmePowerConfigSetCfm* cfm = (CsrWifiSmePowerConfigSetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeSmeConfigSetCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
}

void CsrWifiSmeRegulatoryDomainInfoGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeRoamingConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeMediaStatusIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeMediaStatusInd* ind = (CsrWifiSmeMediaStatusInd*)msg;

    if (priv->smepriv == NULL) {
        unifi_error(priv, "CsrWifiSmeMediaStatusIndSend: invalid smepriv\n");
        return;
    }

    if (ind->mediaStatus == CSR_WIFI_SME_MEDIA_STATUS_CONNECTED) {
        /*
         * Send wireless-extension event up to userland to announce
         * connection.
         */
        wext_send_assoc_event(priv,
                              (unsigned char *)ind->connectionInfo.bssid.a,
                              (unsigned char *)ind->connectionInfo.assocReqInfoElements,
                              ind->connectionInfo.assocReqInfoElementsLength,
                              (unsigned char *)ind->connectionInfo.assocRspInfoElements,
                              ind->connectionInfo.assocRspInfoElementsLength,
                              (unsigned char *)ind->connectionInfo.assocScanInfoElements,
                              ind->connectionInfo.assocScanInfoElementsLength);

	unifi_trace(priv, UDBG2, "CsrWifiSmeMediaStatusIndSend: IBSS=%pM\n",
				 ind->connectionInfo.bssid.a);

        sme_mgt_packet_filter_set(priv);

    } else  {
        /*
         * Send wireless-extension event up to userland to announce
         * connection lost to a BSS.
         */
        wext_send_disassoc_event(priv);
    }
#endif
}

void CsrWifiSmeRoamingConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeRoamingConfigSetCfm* cfm = (CsrWifiSmeRoamingConfigSetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeRoamingConfigSetCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
}

void CsrWifiSmeScanConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeScanConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_SUPPORT_WEXT
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeScanConfigSetCfm* cfm = (CsrWifiSmeScanConfigSetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeSmeConfigSetCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
#endif
}

void CsrWifiSmeStationMacAddressGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeSmeCommonConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeSmeCommonConfigGetCfm* cfm = (CsrWifiSmeSmeCommonConfigGetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeSmeCommonConfigGetCfmSend: Invalid ospriv.\n");
        return;
    }

    priv->sme_reply.deviceConfig = cfm->deviceConfig;
    sme_complete_request(priv, cfm->status);
}

void CsrWifiSmeSmeStaConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeSmeStaConfigGetCfm* cfm = (CsrWifiSmeSmeStaConfigGetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeSmeStaConfigGetCfmSend: Invalid ospriv.\n");
        return;
    }

    priv->sme_reply.staConfig = cfm->smeConfig;
    sme_complete_request(priv, cfm->status);
}

void CsrWifiSmeSmeCommonConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeSmeCommonConfigSetCfm* cfm = (CsrWifiSmeSmeCommonConfigSetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeSmeCommonConfigGetCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
}

void CsrWifiSmeSmeStaConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiSmeSmeStaConfigSetCfm* cfm = (CsrWifiSmeSmeStaConfigSetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiSmeSmeStaConfigGetCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
}

void CsrWifiSmeGetInterfaceCapabilityCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeErrorIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeInfoIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeCoreDumpIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}
void CsrWifiSmeAmpStatusChangeIndHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiSmeActivateCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}
void CsrWifiSmeDeactivateCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

#ifdef CSR_SUPPORT_WEXT
#ifdef CSR_SUPPORT_WEXT_AP
void CsrWifiNmeApStartCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiNmeApStartCfm* cfm = (CsrWifiNmeApStartCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiNmeApStartCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
}

void CsrWifiNmeApStopCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiNmeApStopCfm* cfm = (CsrWifiNmeApStopCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiNmeApStopCfmSend: Invalid ospriv.\n");
        return;
    }

    sme_complete_request(priv, cfm->status);
}

void CsrWifiNmeApConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiNmeApConfigSetCfm* cfm = (CsrWifiNmeApConfigSetCfm*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiNmeApConfigSetCfmSend: Invalid ospriv.\n");
        return;
    }
    sme_complete_request(priv, cfm->status);
}
#endif
#endif
