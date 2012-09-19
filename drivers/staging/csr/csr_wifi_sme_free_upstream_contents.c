/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */
#include <linux/slab.h>
#include "csr_wifi_sme_prim.h"
#include "csr_wifi_sme_lib.h"

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrWifiSmeFreeUpstreamMessageContents
 *
 *  DESCRIPTION
 *
 *
 *  PARAMETERS
 *      eventClass: only the value CSR_WIFI_SME_PRIM will be handled
 *      message:    the message to free
 *----------------------------------------------------------------------------*/
void CsrWifiSmeFreeUpstreamMessageContents(u16 eventClass, void *message)
{
    if (eventClass != CSR_WIFI_SME_PRIM)
    {
        return;
    }
    if (NULL == message)
    {
        return;
    }

    switch (*((CsrWifiSmePrim *) message))
    {
        case CSR_WIFI_SME_ASSOCIATION_COMPLETE_IND:
        {
            CsrWifiSmeAssociationCompleteInd *p = (CsrWifiSmeAssociationCompleteInd *)message;
            kfree(p->connectionInfo.beaconFrame);
            p->connectionInfo.beaconFrame = NULL;
            kfree(p->connectionInfo.associationReqFrame);
            p->connectionInfo.associationReqFrame = NULL;
            kfree(p->connectionInfo.associationRspFrame);
            p->connectionInfo.associationRspFrame = NULL;
            kfree(p->connectionInfo.assocScanInfoElements);
            p->connectionInfo.assocScanInfoElements = NULL;
            kfree(p->connectionInfo.assocReqInfoElements);
            p->connectionInfo.assocReqInfoElements = NULL;
            kfree(p->connectionInfo.assocRspInfoElements);
            p->connectionInfo.assocRspInfoElements = NULL;
            break;
        }
        case CSR_WIFI_SME_BLACKLIST_CFM:
        {
            CsrWifiSmeBlacklistCfm *p = (CsrWifiSmeBlacklistCfm *)message;
            kfree(p->getAddresses);
            p->getAddresses = NULL;
            break;
        }
        case CSR_WIFI_SME_CALIBRATION_DATA_GET_CFM:
        {
            CsrWifiSmeCalibrationDataGetCfm *p = (CsrWifiSmeCalibrationDataGetCfm *)message;
            kfree(p->calibrationData);
            p->calibrationData = NULL;
            break;
        }
        case CSR_WIFI_SME_CONNECTION_CONFIG_GET_CFM:
        {
            CsrWifiSmeConnectionConfigGetCfm *p = (CsrWifiSmeConnectionConfigGetCfm *)message;
            kfree(p->connectionConfig.mlmeAssociateReqInformationElements);
            p->connectionConfig.mlmeAssociateReqInformationElements = NULL;
            break;
        }
        case CSR_WIFI_SME_CONNECTION_INFO_GET_CFM:
        {
            CsrWifiSmeConnectionInfoGetCfm *p = (CsrWifiSmeConnectionInfoGetCfm *)message;
            kfree(p->connectionInfo.beaconFrame);
            p->connectionInfo.beaconFrame = NULL;
            kfree(p->connectionInfo.associationReqFrame);
            p->connectionInfo.associationReqFrame = NULL;
            kfree(p->connectionInfo.associationRspFrame);
            p->connectionInfo.associationRspFrame = NULL;
            kfree(p->connectionInfo.assocScanInfoElements);
            p->connectionInfo.assocScanInfoElements = NULL;
            kfree(p->connectionInfo.assocReqInfoElements);
            p->connectionInfo.assocReqInfoElements = NULL;
            kfree(p->connectionInfo.assocRspInfoElements);
            p->connectionInfo.assocRspInfoElements = NULL;
            break;
        }
        case CSR_WIFI_SME_MEDIA_STATUS_IND:
        {
            CsrWifiSmeMediaStatusInd *p = (CsrWifiSmeMediaStatusInd *)message;
            kfree(p->connectionInfo.beaconFrame);
            p->connectionInfo.beaconFrame = NULL;
            kfree(p->connectionInfo.associationReqFrame);
            p->connectionInfo.associationReqFrame = NULL;
            kfree(p->connectionInfo.associationRspFrame);
            p->connectionInfo.associationRspFrame = NULL;
            kfree(p->connectionInfo.assocScanInfoElements);
            p->connectionInfo.assocScanInfoElements = NULL;
            kfree(p->connectionInfo.assocReqInfoElements);
            p->connectionInfo.assocReqInfoElements = NULL;
            kfree(p->connectionInfo.assocRspInfoElements);
            p->connectionInfo.assocRspInfoElements = NULL;
            break;
        }
        case CSR_WIFI_SME_MIB_GET_CFM:
        {
            CsrWifiSmeMibGetCfm *p = (CsrWifiSmeMibGetCfm *)message;
            kfree(p->mibAttribute);
            p->mibAttribute = NULL;
            break;
        }
        case CSR_WIFI_SME_MIB_GET_NEXT_CFM:
        {
            CsrWifiSmeMibGetNextCfm *p = (CsrWifiSmeMibGetNextCfm *)message;
            kfree(p->mibAttribute);
            p->mibAttribute = NULL;
            break;
        }
        case CSR_WIFI_SME_MULTICAST_ADDRESS_CFM:
        {
            CsrWifiSmeMulticastAddressCfm *p = (CsrWifiSmeMulticastAddressCfm *)message;
            kfree(p->getAddresses);
            p->getAddresses = NULL;
            break;
        }
        case CSR_WIFI_SME_PMKID_CANDIDATE_LIST_IND:
        {
            CsrWifiSmePmkidCandidateListInd *p = (CsrWifiSmePmkidCandidateListInd *)message;
            kfree(p->pmkidCandidates);
            p->pmkidCandidates = NULL;
            break;
        }
        case CSR_WIFI_SME_PMKID_CFM:
        {
            CsrWifiSmePmkidCfm *p = (CsrWifiSmePmkidCfm *)message;
            kfree(p->getPmkids);
            p->getPmkids = NULL;
            break;
        }
        case CSR_WIFI_SME_SCAN_CONFIG_GET_CFM:
        {
            CsrWifiSmeScanConfigGetCfm *p = (CsrWifiSmeScanConfigGetCfm *)message;
            kfree(p->scanConfig.passiveChannelList);
            p->scanConfig.passiveChannelList = NULL;
            break;
        }
        case CSR_WIFI_SME_SCAN_RESULT_IND:
        {
            CsrWifiSmeScanResultInd *p = (CsrWifiSmeScanResultInd *)message;
            kfree(p->result.informationElements);
            p->result.informationElements = NULL;
            switch (p->result.p2pDeviceRole)
            {
                case CSR_WIFI_SME_P2P_ROLE_GO:
                {
                    u16 i4;
                    for (i4 = 0; i4 < p->result.deviceInfo.groupInfo.p2pClientInfoCount; i4++)
                    {
                        kfree(p->result.deviceInfo.groupInfo.p2PClientInfo[i4].clientDeviceInfo.secDeviceType);
                        p->result.deviceInfo.groupInfo.p2PClientInfo[i4].clientDeviceInfo.secDeviceType = NULL;
                    }
                }
                    kfree(p->result.deviceInfo.groupInfo.p2PClientInfo);
                    p->result.deviceInfo.groupInfo.p2PClientInfo = NULL;
                    break;
                case CSR_WIFI_SME_P2P_ROLE_STANDALONE:
                    kfree(p->result.deviceInfo.standalonedevInfo.secDeviceType);
                    p->result.deviceInfo.standalonedevInfo.secDeviceType = NULL;
                    break;
                default:
                    break;
            }
            break;
        }
        case CSR_WIFI_SME_SCAN_RESULTS_GET_CFM:
        {
            CsrWifiSmeScanResultsGetCfm *p = (CsrWifiSmeScanResultsGetCfm *)message;
            {
                u16 i1;
                for (i1 = 0; i1 < p->scanResultsCount; i1++)
                {
                    kfree(p->scanResults[i1].informationElements);
                    p->scanResults[i1].informationElements = NULL;
                    switch (p->scanResults[i1].p2pDeviceRole)
                    {
                        case CSR_WIFI_SME_P2P_ROLE_GO:
                        {
                            u16 i4;
                            for (i4 = 0; i4 < p->scanResults[i1].deviceInfo.groupInfo.p2pClientInfoCount; i4++)
                            {
                                kfree(p->scanResults[i1].deviceInfo.groupInfo.p2PClientInfo[i4].clientDeviceInfo.secDeviceType);
                                p->scanResults[i1].deviceInfo.groupInfo.p2PClientInfo[i4].clientDeviceInfo.secDeviceType = NULL;
                            }
                        }
                            kfree(p->scanResults[i1].deviceInfo.groupInfo.p2PClientInfo);
                            p->scanResults[i1].deviceInfo.groupInfo.p2PClientInfo = NULL;
                            break;
                        case CSR_WIFI_SME_P2P_ROLE_STANDALONE:
                            kfree(p->scanResults[i1].deviceInfo.standalonedevInfo.secDeviceType);
                            p->scanResults[i1].deviceInfo.standalonedevInfo.secDeviceType = NULL;
                            break;
                        default:
                            break;
                    }
                }
            }
            kfree(p->scanResults);
            p->scanResults = NULL;
            break;
        }
        case CSR_WIFI_SME_TSPEC_IND:
        {
            CsrWifiSmeTspecInd *p = (CsrWifiSmeTspecInd *)message;
            kfree(p->tspec);
            p->tspec = NULL;
            break;
        }
        case CSR_WIFI_SME_TSPEC_CFM:
        {
            CsrWifiSmeTspecCfm *p = (CsrWifiSmeTspecCfm *)message;
            kfree(p->tspec);
            p->tspec = NULL;
            break;
        }
        case CSR_WIFI_SME_VERSIONS_GET_CFM:
        {
            CsrWifiSmeVersionsGetCfm *p = (CsrWifiSmeVersionsGetCfm *)message;
            kfree(p->versions.routerBuild);
            p->versions.routerBuild = NULL;
            kfree(p->versions.smeBuild);
            p->versions.smeBuild = NULL;
            break;
        }
        case CSR_WIFI_SME_CLOAKED_SSIDS_GET_CFM:
        {
            CsrWifiSmeCloakedSsidsGetCfm *p = (CsrWifiSmeCloakedSsidsGetCfm *)message;
            kfree(p->cloakedSsids.cloakedSsids);
            p->cloakedSsids.cloakedSsids = NULL;
            break;
        }
        case CSR_WIFI_SME_ERROR_IND:
        {
            CsrWifiSmeErrorInd *p = (CsrWifiSmeErrorInd *)message;
            kfree(p->errorMessage);
            p->errorMessage = NULL;
            break;
        }
        case CSR_WIFI_SME_INFO_IND:
        {
            CsrWifiSmeInfoInd *p = (CsrWifiSmeInfoInd *)message;
            kfree(p->infoMessage);
            p->infoMessage = NULL;
            break;
        }
        case CSR_WIFI_SME_CORE_DUMP_IND:
        {
            CsrWifiSmeCoreDumpInd *p = (CsrWifiSmeCoreDumpInd *)message;
            kfree(p->data);
            p->data = NULL;
            break;
        }

        default:
            break;
    }
}


