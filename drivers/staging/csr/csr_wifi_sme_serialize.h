/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_SME_SERIALIZE_H__
#define CSR_WIFI_SME_SERIALIZE_H__

#include "csr_types.h"
#include "csr_pmem.h"
#include "csr_wifi_msgconv.h"

#include "csr_wifi_sme_prim.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void CsrWifiSmePfree(void *ptr);

#define CsrWifiSmeActivateReqSer CsrWifiEventSer
#define CsrWifiSmeActivateReqDes CsrWifiEventDes
#define CsrWifiSmeActivateReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeActivateReqSerFree CsrWifiSmePfree

#define CsrWifiSmeAdhocConfigGetReqSer CsrWifiEventSer
#define CsrWifiSmeAdhocConfigGetReqDes CsrWifiEventDes
#define CsrWifiSmeAdhocConfigGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeAdhocConfigGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeAdhocConfigSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeAdhocConfigSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeAdhocConfigSetReqSizeof(void *msg);
#define CsrWifiSmeAdhocConfigSetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeBlacklistReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeBlacklistReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeBlacklistReqSizeof(void *msg);
extern void CsrWifiSmeBlacklistReqSerFree(void *msg);

#define CsrWifiSmeCalibrationDataGetReqSer CsrWifiEventSer
#define CsrWifiSmeCalibrationDataGetReqDes CsrWifiEventDes
#define CsrWifiSmeCalibrationDataGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeCalibrationDataGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeCalibrationDataSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeCalibrationDataSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeCalibrationDataSetReqSizeof(void *msg);
extern void CsrWifiSmeCalibrationDataSetReqSerFree(void *msg);

#define CsrWifiSmeCcxConfigGetReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeCcxConfigGetReqDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeCcxConfigGetReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeCcxConfigGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeCcxConfigSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeCcxConfigSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeCcxConfigSetReqSizeof(void *msg);
#define CsrWifiSmeCcxConfigSetReqSerFree CsrWifiSmePfree

#define CsrWifiSmeCoexConfigGetReqSer CsrWifiEventSer
#define CsrWifiSmeCoexConfigGetReqDes CsrWifiEventDes
#define CsrWifiSmeCoexConfigGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeCoexConfigGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeCoexConfigSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeCoexConfigSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeCoexConfigSetReqSizeof(void *msg);
#define CsrWifiSmeCoexConfigSetReqSerFree CsrWifiSmePfree

#define CsrWifiSmeCoexInfoGetReqSer CsrWifiEventSer
#define CsrWifiSmeCoexInfoGetReqDes CsrWifiEventDes
#define CsrWifiSmeCoexInfoGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeCoexInfoGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeConnectReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeConnectReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeConnectReqSizeof(void *msg);
extern void CsrWifiSmeConnectReqSerFree(void *msg);

#define CsrWifiSmeConnectionConfigGetReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeConnectionConfigGetReqDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeConnectionConfigGetReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeConnectionConfigGetReqSerFree CsrWifiSmePfree

#define CsrWifiSmeConnectionInfoGetReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeConnectionInfoGetReqDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeConnectionInfoGetReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeConnectionInfoGetReqSerFree CsrWifiSmePfree

#define CsrWifiSmeConnectionStatsGetReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeConnectionStatsGetReqDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeConnectionStatsGetReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeConnectionStatsGetReqSerFree CsrWifiSmePfree

#define CsrWifiSmeDeactivateReqSer CsrWifiEventSer
#define CsrWifiSmeDeactivateReqDes CsrWifiEventDes
#define CsrWifiSmeDeactivateReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeDeactivateReqSerFree CsrWifiSmePfree

#define CsrWifiSmeDisconnectReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeDisconnectReqDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeDisconnectReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeDisconnectReqSerFree CsrWifiSmePfree

#define CsrWifiSmeEventMaskSetReqSer CsrWifiEventCsrUint32Ser
#define CsrWifiSmeEventMaskSetReqDes CsrWifiEventCsrUint32Des
#define CsrWifiSmeEventMaskSetReqSizeof CsrWifiEventCsrUint32Sizeof
#define CsrWifiSmeEventMaskSetReqSerFree CsrWifiSmePfree

#define CsrWifiSmeHostConfigGetReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeHostConfigGetReqDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeHostConfigGetReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeHostConfigGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeHostConfigSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeHostConfigSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeHostConfigSetReqSizeof(void *msg);
#define CsrWifiSmeHostConfigSetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeKeyReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeKeyReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeKeyReqSizeof(void *msg);
#define CsrWifiSmeKeyReqSerFree CsrWifiSmePfree

#define CsrWifiSmeLinkQualityGetReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeLinkQualityGetReqDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeLinkQualityGetReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeLinkQualityGetReqSerFree CsrWifiSmePfree

#define CsrWifiSmeMibConfigGetReqSer CsrWifiEventSer
#define CsrWifiSmeMibConfigGetReqDes CsrWifiEventDes
#define CsrWifiSmeMibConfigGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeMibConfigGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeMibConfigSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeMibConfigSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeMibConfigSetReqSizeof(void *msg);
#define CsrWifiSmeMibConfigSetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeMibGetNextReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeMibGetNextReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeMibGetNextReqSizeof(void *msg);
extern void CsrWifiSmeMibGetNextReqSerFree(void *msg);

extern CsrUint8* CsrWifiSmeMibGetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeMibGetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeMibGetReqSizeof(void *msg);
extern void CsrWifiSmeMibGetReqSerFree(void *msg);

extern CsrUint8* CsrWifiSmeMibSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeMibSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeMibSetReqSizeof(void *msg);
extern void CsrWifiSmeMibSetReqSerFree(void *msg);

extern CsrUint8* CsrWifiSmeMulticastAddressReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeMulticastAddressReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeMulticastAddressReqSizeof(void *msg);
extern void CsrWifiSmeMulticastAddressReqSerFree(void *msg);

extern CsrUint8* CsrWifiSmePacketFilterSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmePacketFilterSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmePacketFilterSetReqSizeof(void *msg);
extern void CsrWifiSmePacketFilterSetReqSerFree(void *msg);

#define CsrWifiSmePermanentMacAddressGetReqSer CsrWifiEventSer
#define CsrWifiSmePermanentMacAddressGetReqDes CsrWifiEventDes
#define CsrWifiSmePermanentMacAddressGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmePermanentMacAddressGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmePmkidReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmePmkidReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmePmkidReqSizeof(void *msg);
extern void CsrWifiSmePmkidReqSerFree(void *msg);

#define CsrWifiSmePowerConfigGetReqSer CsrWifiEventSer
#define CsrWifiSmePowerConfigGetReqDes CsrWifiEventDes
#define CsrWifiSmePowerConfigGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmePowerConfigGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmePowerConfigSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmePowerConfigSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmePowerConfigSetReqSizeof(void *msg);
#define CsrWifiSmePowerConfigSetReqSerFree CsrWifiSmePfree

#define CsrWifiSmeRegulatoryDomainInfoGetReqSer CsrWifiEventSer
#define CsrWifiSmeRegulatoryDomainInfoGetReqDes CsrWifiEventDes
#define CsrWifiSmeRegulatoryDomainInfoGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeRegulatoryDomainInfoGetReqSerFree CsrWifiSmePfree

#define CsrWifiSmeRoamingConfigGetReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeRoamingConfigGetReqDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeRoamingConfigGetReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeRoamingConfigGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeRoamingConfigSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeRoamingConfigSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeRoamingConfigSetReqSizeof(void *msg);
#define CsrWifiSmeRoamingConfigSetReqSerFree CsrWifiSmePfree

#define CsrWifiSmeScanConfigGetReqSer CsrWifiEventSer
#define CsrWifiSmeScanConfigGetReqDes CsrWifiEventDes
#define CsrWifiSmeScanConfigGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeScanConfigGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeScanConfigSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeScanConfigSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeScanConfigSetReqSizeof(void *msg);
extern void CsrWifiSmeScanConfigSetReqSerFree(void *msg);

extern CsrUint8* CsrWifiSmeScanFullReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeScanFullReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeScanFullReqSizeof(void *msg);
extern void CsrWifiSmeScanFullReqSerFree(void *msg);

#define CsrWifiSmeScanResultsFlushReqSer CsrWifiEventSer
#define CsrWifiSmeScanResultsFlushReqDes CsrWifiEventDes
#define CsrWifiSmeScanResultsFlushReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeScanResultsFlushReqSerFree CsrWifiSmePfree

#define CsrWifiSmeScanResultsGetReqSer CsrWifiEventSer
#define CsrWifiSmeScanResultsGetReqDes CsrWifiEventDes
#define CsrWifiSmeScanResultsGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeScanResultsGetReqSerFree CsrWifiSmePfree

#define CsrWifiSmeSmeStaConfigGetReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeSmeStaConfigGetReqDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeSmeStaConfigGetReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeSmeStaConfigGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeSmeStaConfigSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeSmeStaConfigSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeSmeStaConfigSetReqSizeof(void *msg);
#define CsrWifiSmeSmeStaConfigSetReqSerFree CsrWifiSmePfree

#define CsrWifiSmeStationMacAddressGetReqSer CsrWifiEventSer
#define CsrWifiSmeStationMacAddressGetReqDes CsrWifiEventDes
#define CsrWifiSmeStationMacAddressGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeStationMacAddressGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeTspecReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeTspecReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeTspecReqSizeof(void *msg);
extern void CsrWifiSmeTspecReqSerFree(void *msg);

#define CsrWifiSmeVersionsGetReqSer CsrWifiEventSer
#define CsrWifiSmeVersionsGetReqDes CsrWifiEventDes
#define CsrWifiSmeVersionsGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeVersionsGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeWifiFlightmodeReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeWifiFlightmodeReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeWifiFlightmodeReqSizeof(void *msg);
extern void CsrWifiSmeWifiFlightmodeReqSerFree(void *msg);

#define CsrWifiSmeWifiOffReqSer CsrWifiEventSer
#define CsrWifiSmeWifiOffReqDes CsrWifiEventDes
#define CsrWifiSmeWifiOffReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeWifiOffReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeWifiOnReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeWifiOnReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeWifiOnReqSizeof(void *msg);
extern void CsrWifiSmeWifiOnReqSerFree(void *msg);

extern CsrUint8* CsrWifiSmeCloakedSsidsSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeCloakedSsidsSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeCloakedSsidsSetReqSizeof(void *msg);
extern void CsrWifiSmeCloakedSsidsSetReqSerFree(void *msg);

#define CsrWifiSmeCloakedSsidsGetReqSer CsrWifiEventSer
#define CsrWifiSmeCloakedSsidsGetReqDes CsrWifiEventDes
#define CsrWifiSmeCloakedSsidsGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeCloakedSsidsGetReqSerFree CsrWifiSmePfree

#define CsrWifiSmeSmeCommonConfigGetReqSer CsrWifiEventSer
#define CsrWifiSmeSmeCommonConfigGetReqDes CsrWifiEventDes
#define CsrWifiSmeSmeCommonConfigGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeSmeCommonConfigGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeSmeCommonConfigSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeSmeCommonConfigSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeSmeCommonConfigSetReqSizeof(void *msg);
#define CsrWifiSmeSmeCommonConfigSetReqSerFree CsrWifiSmePfree

#define CsrWifiSmeInterfaceCapabilityGetReqSer CsrWifiEventSer
#define CsrWifiSmeInterfaceCapabilityGetReqDes CsrWifiEventDes
#define CsrWifiSmeInterfaceCapabilityGetReqSizeof CsrWifiEventSizeof
#define CsrWifiSmeInterfaceCapabilityGetReqSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeWpsConfigurationReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeWpsConfigurationReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeWpsConfigurationReqSizeof(void *msg);
extern void CsrWifiSmeWpsConfigurationReqSerFree(void *msg);

extern CsrUint8* CsrWifiSmeSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeSetReqSizeof(void *msg);
extern void CsrWifiSmeSetReqSerFree(void *msg);

#define CsrWifiSmeActivateCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeActivateCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeActivateCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeActivateCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeAdhocConfigGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeAdhocConfigGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeAdhocConfigGetCfmSizeof(void *msg);
#define CsrWifiSmeAdhocConfigGetCfmSerFree CsrWifiSmePfree

#define CsrWifiSmeAdhocConfigSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeAdhocConfigSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeAdhocConfigSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeAdhocConfigSetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeAssociationCompleteIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeAssociationCompleteIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeAssociationCompleteIndSizeof(void *msg);
extern void CsrWifiSmeAssociationCompleteIndSerFree(void *msg);

extern CsrUint8* CsrWifiSmeAssociationStartIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeAssociationStartIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeAssociationStartIndSizeof(void *msg);
#define CsrWifiSmeAssociationStartIndSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeBlacklistCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeBlacklistCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeBlacklistCfmSizeof(void *msg);
extern void CsrWifiSmeBlacklistCfmSerFree(void *msg);

extern CsrUint8* CsrWifiSmeCalibrationDataGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeCalibrationDataGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeCalibrationDataGetCfmSizeof(void *msg);
extern void CsrWifiSmeCalibrationDataGetCfmSerFree(void *msg);

#define CsrWifiSmeCalibrationDataSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeCalibrationDataSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeCalibrationDataSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeCalibrationDataSetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeCcxConfigGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeCcxConfigGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeCcxConfigGetCfmSizeof(void *msg);
#define CsrWifiSmeCcxConfigGetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeCcxConfigSetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeCcxConfigSetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeCcxConfigSetCfmSizeof(void *msg);
#define CsrWifiSmeCcxConfigSetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeCoexConfigGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeCoexConfigGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeCoexConfigGetCfmSizeof(void *msg);
#define CsrWifiSmeCoexConfigGetCfmSerFree CsrWifiSmePfree

#define CsrWifiSmeCoexConfigSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeCoexConfigSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeCoexConfigSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeCoexConfigSetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeCoexInfoGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeCoexInfoGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeCoexInfoGetCfmSizeof(void *msg);
#define CsrWifiSmeCoexInfoGetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeConnectCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeConnectCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeConnectCfmSizeof(void *msg);
#define CsrWifiSmeConnectCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeConnectionConfigGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeConnectionConfigGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeConnectionConfigGetCfmSizeof(void *msg);
extern void CsrWifiSmeConnectionConfigGetCfmSerFree(void *msg);

extern CsrUint8* CsrWifiSmeConnectionInfoGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeConnectionInfoGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeConnectionInfoGetCfmSizeof(void *msg);
extern void CsrWifiSmeConnectionInfoGetCfmSerFree(void *msg);

extern CsrUint8* CsrWifiSmeConnectionQualityIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeConnectionQualityIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeConnectionQualityIndSizeof(void *msg);
#define CsrWifiSmeConnectionQualityIndSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeConnectionStatsGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeConnectionStatsGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeConnectionStatsGetCfmSizeof(void *msg);
#define CsrWifiSmeConnectionStatsGetCfmSerFree CsrWifiSmePfree

#define CsrWifiSmeDeactivateCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeDeactivateCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeDeactivateCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeDeactivateCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeDisconnectCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeDisconnectCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeDisconnectCfmSizeof(void *msg);
#define CsrWifiSmeDisconnectCfmSerFree CsrWifiSmePfree

#define CsrWifiSmeEventMaskSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeEventMaskSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeEventMaskSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeEventMaskSetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeHostConfigGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeHostConfigGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeHostConfigGetCfmSizeof(void *msg);
#define CsrWifiSmeHostConfigGetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeHostConfigSetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeHostConfigSetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeHostConfigSetCfmSizeof(void *msg);
#define CsrWifiSmeHostConfigSetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeIbssStationIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeIbssStationIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeIbssStationIndSizeof(void *msg);
#define CsrWifiSmeIbssStationIndSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeKeyCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeKeyCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeKeyCfmSizeof(void *msg);
#define CsrWifiSmeKeyCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeLinkQualityGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeLinkQualityGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeLinkQualityGetCfmSizeof(void *msg);
#define CsrWifiSmeLinkQualityGetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeMediaStatusIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeMediaStatusIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeMediaStatusIndSizeof(void *msg);
extern void CsrWifiSmeMediaStatusIndSerFree(void *msg);

extern CsrUint8* CsrWifiSmeMibConfigGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeMibConfigGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeMibConfigGetCfmSizeof(void *msg);
#define CsrWifiSmeMibConfigGetCfmSerFree CsrWifiSmePfree

#define CsrWifiSmeMibConfigSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeMibConfigSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeMibConfigSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeMibConfigSetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeMibGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeMibGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeMibGetCfmSizeof(void *msg);
extern void CsrWifiSmeMibGetCfmSerFree(void *msg);

extern CsrUint8* CsrWifiSmeMibGetNextCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeMibGetNextCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeMibGetNextCfmSizeof(void *msg);
extern void CsrWifiSmeMibGetNextCfmSerFree(void *msg);

#define CsrWifiSmeMibSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeMibSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeMibSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeMibSetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeMicFailureIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeMicFailureIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeMicFailureIndSizeof(void *msg);
#define CsrWifiSmeMicFailureIndSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeMulticastAddressCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeMulticastAddressCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeMulticastAddressCfmSizeof(void *msg);
extern void CsrWifiSmeMulticastAddressCfmSerFree(void *msg);

extern CsrUint8* CsrWifiSmePacketFilterSetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmePacketFilterSetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmePacketFilterSetCfmSizeof(void *msg);
#define CsrWifiSmePacketFilterSetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmePermanentMacAddressGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmePermanentMacAddressGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmePermanentMacAddressGetCfmSizeof(void *msg);
#define CsrWifiSmePermanentMacAddressGetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmePmkidCandidateListIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmePmkidCandidateListIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmePmkidCandidateListIndSizeof(void *msg);
extern void CsrWifiSmePmkidCandidateListIndSerFree(void *msg);

extern CsrUint8* CsrWifiSmePmkidCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmePmkidCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmePmkidCfmSizeof(void *msg);
extern void CsrWifiSmePmkidCfmSerFree(void *msg);

extern CsrUint8* CsrWifiSmePowerConfigGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmePowerConfigGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmePowerConfigGetCfmSizeof(void *msg);
#define CsrWifiSmePowerConfigGetCfmSerFree CsrWifiSmePfree

#define CsrWifiSmePowerConfigSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmePowerConfigSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmePowerConfigSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmePowerConfigSetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeRegulatoryDomainInfoGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeRegulatoryDomainInfoGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeRegulatoryDomainInfoGetCfmSizeof(void *msg);
#define CsrWifiSmeRegulatoryDomainInfoGetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeRoamCompleteIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeRoamCompleteIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeRoamCompleteIndSizeof(void *msg);
#define CsrWifiSmeRoamCompleteIndSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeRoamStartIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeRoamStartIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeRoamStartIndSizeof(void *msg);
#define CsrWifiSmeRoamStartIndSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeRoamingConfigGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeRoamingConfigGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeRoamingConfigGetCfmSizeof(void *msg);
#define CsrWifiSmeRoamingConfigGetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeRoamingConfigSetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeRoamingConfigSetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeRoamingConfigSetCfmSizeof(void *msg);
#define CsrWifiSmeRoamingConfigSetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeScanConfigGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeScanConfigGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeScanConfigGetCfmSizeof(void *msg);
extern void CsrWifiSmeScanConfigGetCfmSerFree(void *msg);

#define CsrWifiSmeScanConfigSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeScanConfigSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeScanConfigSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeScanConfigSetCfmSerFree CsrWifiSmePfree

#define CsrWifiSmeScanFullCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeScanFullCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeScanFullCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeScanFullCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeScanResultIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeScanResultIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeScanResultIndSizeof(void *msg);
extern void CsrWifiSmeScanResultIndSerFree(void *msg);

#define CsrWifiSmeScanResultsFlushCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeScanResultsFlushCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeScanResultsFlushCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeScanResultsFlushCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeScanResultsGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeScanResultsGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeScanResultsGetCfmSizeof(void *msg);
extern void CsrWifiSmeScanResultsGetCfmSerFree(void *msg);

extern CsrUint8* CsrWifiSmeSmeStaConfigGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeSmeStaConfigGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeSmeStaConfigGetCfmSizeof(void *msg);
#define CsrWifiSmeSmeStaConfigGetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeSmeStaConfigSetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeSmeStaConfigSetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeSmeStaConfigSetCfmSizeof(void *msg);
#define CsrWifiSmeSmeStaConfigSetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeStationMacAddressGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeStationMacAddressGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeStationMacAddressGetCfmSizeof(void *msg);
#define CsrWifiSmeStationMacAddressGetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeTspecIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeTspecIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeTspecIndSizeof(void *msg);
extern void CsrWifiSmeTspecIndSerFree(void *msg);

extern CsrUint8* CsrWifiSmeTspecCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeTspecCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeTspecCfmSizeof(void *msg);
extern void CsrWifiSmeTspecCfmSerFree(void *msg);

extern CsrUint8* CsrWifiSmeVersionsGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeVersionsGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeVersionsGetCfmSizeof(void *msg);
extern void CsrWifiSmeVersionsGetCfmSerFree(void *msg);

#define CsrWifiSmeWifiFlightmodeCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeWifiFlightmodeCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeWifiFlightmodeCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeWifiFlightmodeCfmSerFree CsrWifiSmePfree

#define CsrWifiSmeWifiOffIndSer CsrWifiEventCsrUint8Ser
#define CsrWifiSmeWifiOffIndDes CsrWifiEventCsrUint8Des
#define CsrWifiSmeWifiOffIndSizeof CsrWifiEventCsrUint8Sizeof
#define CsrWifiSmeWifiOffIndSerFree CsrWifiSmePfree

#define CsrWifiSmeWifiOffCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeWifiOffCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeWifiOffCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeWifiOffCfmSerFree CsrWifiSmePfree

#define CsrWifiSmeWifiOnCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeWifiOnCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeWifiOnCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeWifiOnCfmSerFree CsrWifiSmePfree

#define CsrWifiSmeCloakedSsidsSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeCloakedSsidsSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeCloakedSsidsSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeCloakedSsidsSetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeCloakedSsidsGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeCloakedSsidsGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeCloakedSsidsGetCfmSizeof(void *msg);
extern void CsrWifiSmeCloakedSsidsGetCfmSerFree(void *msg);

extern CsrUint8* CsrWifiSmeWifiOnIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeWifiOnIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeWifiOnIndSizeof(void *msg);
#define CsrWifiSmeWifiOnIndSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeSmeCommonConfigGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeSmeCommonConfigGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeSmeCommonConfigGetCfmSizeof(void *msg);
#define CsrWifiSmeSmeCommonConfigGetCfmSerFree CsrWifiSmePfree

#define CsrWifiSmeSmeCommonConfigSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeSmeCommonConfigSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeSmeCommonConfigSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeSmeCommonConfigSetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeInterfaceCapabilityGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeInterfaceCapabilityGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeInterfaceCapabilityGetCfmSizeof(void *msg);
#define CsrWifiSmeInterfaceCapabilityGetCfmSerFree CsrWifiSmePfree

extern CsrUint8* CsrWifiSmeErrorIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeErrorIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeErrorIndSizeof(void *msg);
extern void CsrWifiSmeErrorIndSerFree(void *msg);

extern CsrUint8* CsrWifiSmeInfoIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeInfoIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeInfoIndSizeof(void *msg);
extern void CsrWifiSmeInfoIndSerFree(void *msg);

extern CsrUint8* CsrWifiSmeCoreDumpIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiSmeCoreDumpIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiSmeCoreDumpIndSizeof(void *msg);
extern void CsrWifiSmeCoreDumpIndSerFree(void *msg);

#define CsrWifiSmeAmpStatusChangeIndSer CsrWifiEventCsrUint16CsrUint8Ser
#define CsrWifiSmeAmpStatusChangeIndDes CsrWifiEventCsrUint16CsrUint8Des
#define CsrWifiSmeAmpStatusChangeIndSizeof CsrWifiEventCsrUint16CsrUint8Sizeof
#define CsrWifiSmeAmpStatusChangeIndSerFree CsrWifiSmePfree

#define CsrWifiSmeWpsConfigurationCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiSmeWpsConfigurationCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiSmeWpsConfigurationCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiSmeWpsConfigurationCfmSerFree CsrWifiSmePfree


#ifdef __cplusplus
}
#endif
#endif /* CSR_WIFI_SME_SERIALIZE_H__ */

