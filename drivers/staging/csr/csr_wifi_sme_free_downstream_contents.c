/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#include "csr_pmem.h"
#include "csr_wifi_sme_prim.h"
#include "csr_wifi_sme_lib.h"

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrWifiSmeFreeDownstreamMessageContents
 *
 *  DESCRIPTION
 *
 *
 *  PARAMETERS
 *      eventClass: only the value CSR_WIFI_SME_PRIM will be handled
 *      message:    the message to free
 *----------------------------------------------------------------------------*/
void CsrWifiSmeFreeDownstreamMessageContents(u16 eventClass, void *message)
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
        case CSR_WIFI_SME_BLACKLIST_REQ:
        {
            CsrWifiSmeBlacklistReq *p = (CsrWifiSmeBlacklistReq *)message;
            CsrPmemFree(p->setAddresses);
            p->setAddresses = NULL;
            break;
        }
        case CSR_WIFI_SME_CALIBRATION_DATA_SET_REQ:
        {
            CsrWifiSmeCalibrationDataSetReq *p = (CsrWifiSmeCalibrationDataSetReq *)message;
            CsrPmemFree(p->calibrationData);
            p->calibrationData = NULL;
            break;
        }
        case CSR_WIFI_SME_CONNECT_REQ:
        {
            CsrWifiSmeConnectReq *p = (CsrWifiSmeConnectReq *)message;
            CsrPmemFree(p->connectionConfig.mlmeAssociateReqInformationElements);
            p->connectionConfig.mlmeAssociateReqInformationElements = NULL;
            break;
        }
        case CSR_WIFI_SME_MIB_GET_NEXT_REQ:
        {
            CsrWifiSmeMibGetNextReq *p = (CsrWifiSmeMibGetNextReq *)message;
            CsrPmemFree(p->mibAttribute);
            p->mibAttribute = NULL;
            break;
        }
        case CSR_WIFI_SME_MIB_GET_REQ:
        {
            CsrWifiSmeMibGetReq *p = (CsrWifiSmeMibGetReq *)message;
            CsrPmemFree(p->mibAttribute);
            p->mibAttribute = NULL;
            break;
        }
        case CSR_WIFI_SME_MIB_SET_REQ:
        {
            CsrWifiSmeMibSetReq *p = (CsrWifiSmeMibSetReq *)message;
            CsrPmemFree(p->mibAttribute);
            p->mibAttribute = NULL;
            break;
        }
        case CSR_WIFI_SME_MULTICAST_ADDRESS_REQ:
        {
            CsrWifiSmeMulticastAddressReq *p = (CsrWifiSmeMulticastAddressReq *)message;
            CsrPmemFree(p->setAddresses);
            p->setAddresses = NULL;
            break;
        }
        case CSR_WIFI_SME_PACKET_FILTER_SET_REQ:
        {
            CsrWifiSmePacketFilterSetReq *p = (CsrWifiSmePacketFilterSetReq *)message;
            CsrPmemFree(p->filter);
            p->filter = NULL;
            break;
        }
        case CSR_WIFI_SME_PMKID_REQ:
        {
            CsrWifiSmePmkidReq *p = (CsrWifiSmePmkidReq *)message;
            CsrPmemFree(p->setPmkids);
            p->setPmkids = NULL;
            break;
        }
        case CSR_WIFI_SME_SCAN_CONFIG_SET_REQ:
        {
            CsrWifiSmeScanConfigSetReq *p = (CsrWifiSmeScanConfigSetReq *)message;
            CsrPmemFree(p->scanConfig.passiveChannelList);
            p->scanConfig.passiveChannelList = NULL;
            break;
        }
        case CSR_WIFI_SME_SCAN_FULL_REQ:
        {
            CsrWifiSmeScanFullReq *p = (CsrWifiSmeScanFullReq *)message;
            CsrPmemFree(p->ssid);
            p->ssid = NULL;
            CsrPmemFree(p->channelList);
            p->channelList = NULL;
            CsrPmemFree(p->probeIe);
            p->probeIe = NULL;
            break;
        }
        case CSR_WIFI_SME_TSPEC_REQ:
        {
            CsrWifiSmeTspecReq *p = (CsrWifiSmeTspecReq *)message;
            CsrPmemFree(p->tspec);
            p->tspec = NULL;
            CsrPmemFree(p->tclas);
            p->tclas = NULL;
            break;
        }
        case CSR_WIFI_SME_WIFI_FLIGHTMODE_REQ:
        {
            CsrWifiSmeWifiFlightmodeReq *p = (CsrWifiSmeWifiFlightmodeReq *)message;
            {
                u16 i1;
                for (i1 = 0; i1 < p->mibFilesCount; i1++)
                {
                    CsrPmemFree(p->mibFiles[i1].data);
                    p->mibFiles[i1].data = NULL;
                }
            }
            CsrPmemFree(p->mibFiles);
            p->mibFiles = NULL;
            break;
        }
        case CSR_WIFI_SME_WIFI_ON_REQ:
        {
            CsrWifiSmeWifiOnReq *p = (CsrWifiSmeWifiOnReq *)message;
            {
                u16 i1;
                for (i1 = 0; i1 < p->mibFilesCount; i1++)
                {
                    CsrPmemFree(p->mibFiles[i1].data);
                    p->mibFiles[i1].data = NULL;
                }
            }
            CsrPmemFree(p->mibFiles);
            p->mibFiles = NULL;
            break;
        }
        case CSR_WIFI_SME_CLOAKED_SSIDS_SET_REQ:
        {
            CsrWifiSmeCloakedSsidsSetReq *p = (CsrWifiSmeCloakedSsidsSetReq *)message;
            CsrPmemFree(p->cloakedSsids.cloakedSsids);
            p->cloakedSsids.cloakedSsids = NULL;
            break;
        }
        case CSR_WIFI_SME_WPS_CONFIGURATION_REQ:
        {
            CsrWifiSmeWpsConfigurationReq *p = (CsrWifiSmeWpsConfigurationReq *)message;
            CsrPmemFree(p->wpsConfig.secondaryDeviceType);
            p->wpsConfig.secondaryDeviceType = NULL;
            break;
        }
        case CSR_WIFI_SME_SET_REQ:
        {
            CsrWifiSmeSetReq *p = (CsrWifiSmeSetReq *)message;
            CsrPmemFree(p->data);
            p->data = NULL;
            break;
        }

        default:
            break;
    }
}


