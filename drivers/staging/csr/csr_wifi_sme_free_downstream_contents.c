/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */
#include <linux/slab.h>
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
            kfree(p->setAddresses);
            p->setAddresses = NULL;
            break;
        }
        case CSR_WIFI_SME_CALIBRATION_DATA_SET_REQ:
        {
            CsrWifiSmeCalibrationDataSetReq *p = (CsrWifiSmeCalibrationDataSetReq *)message;
            kfree(p->calibrationData);
            p->calibrationData = NULL;
            break;
        }
        case CSR_WIFI_SME_CONNECT_REQ:
        {
            CsrWifiSmeConnectReq *p = (CsrWifiSmeConnectReq *)message;
            kfree(p->connectionConfig.mlmeAssociateReqInformationElements);
            p->connectionConfig.mlmeAssociateReqInformationElements = NULL;
            break;
        }
        case CSR_WIFI_SME_MIB_GET_NEXT_REQ:
        {
            CsrWifiSmeMibGetNextReq *p = (CsrWifiSmeMibGetNextReq *)message;
            kfree(p->mibAttribute);
            p->mibAttribute = NULL;
            break;
        }
        case CSR_WIFI_SME_MIB_GET_REQ:
        {
            CsrWifiSmeMibGetReq *p = (CsrWifiSmeMibGetReq *)message;
            kfree(p->mibAttribute);
            p->mibAttribute = NULL;
            break;
        }
        case CSR_WIFI_SME_MIB_SET_REQ:
        {
            CsrWifiSmeMibSetReq *p = (CsrWifiSmeMibSetReq *)message;
            kfree(p->mibAttribute);
            p->mibAttribute = NULL;
            break;
        }
        case CSR_WIFI_SME_MULTICAST_ADDRESS_REQ:
        {
            CsrWifiSmeMulticastAddressReq *p = (CsrWifiSmeMulticastAddressReq *)message;
            kfree(p->setAddresses);
            p->setAddresses = NULL;
            break;
        }
        case CSR_WIFI_SME_PACKET_FILTER_SET_REQ:
        {
            CsrWifiSmePacketFilterSetReq *p = (CsrWifiSmePacketFilterSetReq *)message;
            kfree(p->filter);
            p->filter = NULL;
            break;
        }
        case CSR_WIFI_SME_PMKID_REQ:
        {
            CsrWifiSmePmkidReq *p = (CsrWifiSmePmkidReq *)message;
            kfree(p->setPmkids);
            p->setPmkids = NULL;
            break;
        }
        case CSR_WIFI_SME_SCAN_CONFIG_SET_REQ:
        {
            CsrWifiSmeScanConfigSetReq *p = (CsrWifiSmeScanConfigSetReq *)message;
            kfree(p->scanConfig.passiveChannelList);
            p->scanConfig.passiveChannelList = NULL;
            break;
        }
        case CSR_WIFI_SME_SCAN_FULL_REQ:
        {
            CsrWifiSmeScanFullReq *p = (CsrWifiSmeScanFullReq *)message;
            kfree(p->ssid);
            p->ssid = NULL;
            kfree(p->channelList);
            p->channelList = NULL;
            kfree(p->probeIe);
            p->probeIe = NULL;
            break;
        }
        case CSR_WIFI_SME_TSPEC_REQ:
        {
            CsrWifiSmeTspecReq *p = (CsrWifiSmeTspecReq *)message;
            kfree(p->tspec);
            p->tspec = NULL;
            kfree(p->tclas);
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
                    kfree(p->mibFiles[i1].data);
                    p->mibFiles[i1].data = NULL;
                }
            }
            kfree(p->mibFiles);
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
                    kfree(p->mibFiles[i1].data);
                    p->mibFiles[i1].data = NULL;
                }
            }
            kfree(p->mibFiles);
            p->mibFiles = NULL;
            break;
        }
        case CSR_WIFI_SME_CLOAKED_SSIDS_SET_REQ:
        {
            CsrWifiSmeCloakedSsidsSetReq *p = (CsrWifiSmeCloakedSsidsSetReq *)message;
            kfree(p->cloakedSsids.cloakedSsids);
            p->cloakedSsids.cloakedSsids = NULL;
            break;
        }
        case CSR_WIFI_SME_WPS_CONFIGURATION_REQ:
        {
            CsrWifiSmeWpsConfigurationReq *p = (CsrWifiSmeWpsConfigurationReq *)message;
            kfree(p->wpsConfig.secondaryDeviceType);
            p->wpsConfig.secondaryDeviceType = NULL;
            break;
        }
        case CSR_WIFI_SME_SET_REQ:
        {
            CsrWifiSmeSetReq *p = (CsrWifiSmeSetReq *)message;
            kfree(p->data);
            p->data = NULL;
            break;
        }

        default:
            break;
    }
}


