/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            Confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/
#ifndef CSR_WIFI_ROUTER_SEF_CSR_WIFI_SME_H__
#define CSR_WIFI_ROUTER_SEF_CSR_WIFI_SME_H__

#include "csr_wifi_sme_prim.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef void (*CsrWifiSmeStateHandlerType)(void* drvpriv, CsrWifiFsmEvent* msg);

extern const CsrWifiSmeStateHandlerType CsrWifiSmeUpstreamStateHandlers[CSR_WIFI_SME_PRIM_UPSTREAM_COUNT];


extern void CsrWifiSmeActivateCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeAdhocConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeAdhocConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeAssociationCompleteIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeAssociationStartIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeBlacklistCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeCalibrationDataGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeCalibrationDataSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeCcxConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeCcxConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeCoexConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeCoexConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeCoexInfoGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeConnectCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeConnectionConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeConnectionInfoGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeConnectionQualityIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeConnectionStatsGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeDeactivateCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeDisconnectCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeEventMaskSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeHostConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeHostConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeIbssStationIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeKeyCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeLinkQualityGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeMediaStatusIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeMibConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeMibConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeMibGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeMibGetNextCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeMibSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeMicFailureIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeMulticastAddressCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmePacketFilterSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmePermanentMacAddressGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmePmkidCandidateListIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmePmkidCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmePowerConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmePowerConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeRegulatoryDomainInfoGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeRoamCompleteIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeRoamStartIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeRoamingConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeRoamingConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeScanConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeScanConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeScanFullCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeScanResultIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeScanResultsFlushCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeScanResultsGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeSmeStaConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeSmeStaConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeStationMacAddressGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeTspecIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeTspecCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeVersionsGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeWifiFlightmodeCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeWifiOffIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeWifiOffCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeWifiOnCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeCloakedSsidsSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeCloakedSsidsGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeWifiOnIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeSmeCommonConfigGetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeSmeCommonConfigSetCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeGetInterfaceCapabilityCfmHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeErrorIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeInfoIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeCoreDumpIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);
extern void CsrWifiSmeAmpStatusChangeIndHandler(void* drvpriv, CsrWifiFsmEvent* msg);

#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_ROUTER_SEF_CSR_WIFI_SME_H__ */
