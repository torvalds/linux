/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#include "csr_msgconv.h"
#include "csr_macro.h"


#ifdef CSR_LOG_ENABLE
#include "csr_log.h"
#endif

#ifndef EXCLUDE_CSR_WIFI_SME_MODULE
#include "csr_wifi_sme_serialize.h"
#include "csr_wifi_sme_prim.h"

static CsrMsgConvMsgEntry csrwifisme_conv_lut[] = {
    { CSR_WIFI_SME_ACTIVATE_REQ, CsrWifiSmeActivateReqSizeof, CsrWifiSmeActivateReqSer, CsrWifiSmeActivateReqDes, CsrWifiSmeActivateReqSerFree },
    { CSR_WIFI_SME_ADHOC_CONFIG_GET_REQ, CsrWifiSmeAdhocConfigGetReqSizeof, CsrWifiSmeAdhocConfigGetReqSer, CsrWifiSmeAdhocConfigGetReqDes, CsrWifiSmeAdhocConfigGetReqSerFree },
    { CSR_WIFI_SME_ADHOC_CONFIG_SET_REQ, CsrWifiSmeAdhocConfigSetReqSizeof, CsrWifiSmeAdhocConfigSetReqSer, CsrWifiSmeAdhocConfigSetReqDes, CsrWifiSmeAdhocConfigSetReqSerFree },
    { CSR_WIFI_SME_BLACKLIST_REQ, CsrWifiSmeBlacklistReqSizeof, CsrWifiSmeBlacklistReqSer, CsrWifiSmeBlacklistReqDes, CsrWifiSmeBlacklistReqSerFree },
    { CSR_WIFI_SME_CALIBRATION_DATA_GET_REQ, CsrWifiSmeCalibrationDataGetReqSizeof, CsrWifiSmeCalibrationDataGetReqSer, CsrWifiSmeCalibrationDataGetReqDes, CsrWifiSmeCalibrationDataGetReqSerFree },
    { CSR_WIFI_SME_CALIBRATION_DATA_SET_REQ, CsrWifiSmeCalibrationDataSetReqSizeof, CsrWifiSmeCalibrationDataSetReqSer, CsrWifiSmeCalibrationDataSetReqDes, CsrWifiSmeCalibrationDataSetReqSerFree },
    { CSR_WIFI_SME_CCX_CONFIG_GET_REQ, CsrWifiSmeCcxConfigGetReqSizeof, CsrWifiSmeCcxConfigGetReqSer, CsrWifiSmeCcxConfigGetReqDes, CsrWifiSmeCcxConfigGetReqSerFree },
    { CSR_WIFI_SME_CCX_CONFIG_SET_REQ, CsrWifiSmeCcxConfigSetReqSizeof, CsrWifiSmeCcxConfigSetReqSer, CsrWifiSmeCcxConfigSetReqDes, CsrWifiSmeCcxConfigSetReqSerFree },
    { CSR_WIFI_SME_COEX_CONFIG_GET_REQ, CsrWifiSmeCoexConfigGetReqSizeof, CsrWifiSmeCoexConfigGetReqSer, CsrWifiSmeCoexConfigGetReqDes, CsrWifiSmeCoexConfigGetReqSerFree },
    { CSR_WIFI_SME_COEX_CONFIG_SET_REQ, CsrWifiSmeCoexConfigSetReqSizeof, CsrWifiSmeCoexConfigSetReqSer, CsrWifiSmeCoexConfigSetReqDes, CsrWifiSmeCoexConfigSetReqSerFree },
    { CSR_WIFI_SME_COEX_INFO_GET_REQ, CsrWifiSmeCoexInfoGetReqSizeof, CsrWifiSmeCoexInfoGetReqSer, CsrWifiSmeCoexInfoGetReqDes, CsrWifiSmeCoexInfoGetReqSerFree },
    { CSR_WIFI_SME_CONNECT_REQ, CsrWifiSmeConnectReqSizeof, CsrWifiSmeConnectReqSer, CsrWifiSmeConnectReqDes, CsrWifiSmeConnectReqSerFree },
    { CSR_WIFI_SME_CONNECTION_CONFIG_GET_REQ, CsrWifiSmeConnectionConfigGetReqSizeof, CsrWifiSmeConnectionConfigGetReqSer, CsrWifiSmeConnectionConfigGetReqDes, CsrWifiSmeConnectionConfigGetReqSerFree },
    { CSR_WIFI_SME_CONNECTION_INFO_GET_REQ, CsrWifiSmeConnectionInfoGetReqSizeof, CsrWifiSmeConnectionInfoGetReqSer, CsrWifiSmeConnectionInfoGetReqDes, CsrWifiSmeConnectionInfoGetReqSerFree },
    { CSR_WIFI_SME_CONNECTION_STATS_GET_REQ, CsrWifiSmeConnectionStatsGetReqSizeof, CsrWifiSmeConnectionStatsGetReqSer, CsrWifiSmeConnectionStatsGetReqDes, CsrWifiSmeConnectionStatsGetReqSerFree },
    { CSR_WIFI_SME_DEACTIVATE_REQ, CsrWifiSmeDeactivateReqSizeof, CsrWifiSmeDeactivateReqSer, CsrWifiSmeDeactivateReqDes, CsrWifiSmeDeactivateReqSerFree },
    { CSR_WIFI_SME_DISCONNECT_REQ, CsrWifiSmeDisconnectReqSizeof, CsrWifiSmeDisconnectReqSer, CsrWifiSmeDisconnectReqDes, CsrWifiSmeDisconnectReqSerFree },
    { CSR_WIFI_SME_EVENT_MASK_SET_REQ, CsrWifiSmeEventMaskSetReqSizeof, CsrWifiSmeEventMaskSetReqSer, CsrWifiSmeEventMaskSetReqDes, CsrWifiSmeEventMaskSetReqSerFree },
    { CSR_WIFI_SME_HOST_CONFIG_GET_REQ, CsrWifiSmeHostConfigGetReqSizeof, CsrWifiSmeHostConfigGetReqSer, CsrWifiSmeHostConfigGetReqDes, CsrWifiSmeHostConfigGetReqSerFree },
    { CSR_WIFI_SME_HOST_CONFIG_SET_REQ, CsrWifiSmeHostConfigSetReqSizeof, CsrWifiSmeHostConfigSetReqSer, CsrWifiSmeHostConfigSetReqDes, CsrWifiSmeHostConfigSetReqSerFree },
    { CSR_WIFI_SME_KEY_REQ, CsrWifiSmeKeyReqSizeof, CsrWifiSmeKeyReqSer, CsrWifiSmeKeyReqDes, CsrWifiSmeKeyReqSerFree },
    { CSR_WIFI_SME_LINK_QUALITY_GET_REQ, CsrWifiSmeLinkQualityGetReqSizeof, CsrWifiSmeLinkQualityGetReqSer, CsrWifiSmeLinkQualityGetReqDes, CsrWifiSmeLinkQualityGetReqSerFree },
    { CSR_WIFI_SME_MIB_CONFIG_GET_REQ, CsrWifiSmeMibConfigGetReqSizeof, CsrWifiSmeMibConfigGetReqSer, CsrWifiSmeMibConfigGetReqDes, CsrWifiSmeMibConfigGetReqSerFree },
    { CSR_WIFI_SME_MIB_CONFIG_SET_REQ, CsrWifiSmeMibConfigSetReqSizeof, CsrWifiSmeMibConfigSetReqSer, CsrWifiSmeMibConfigSetReqDes, CsrWifiSmeMibConfigSetReqSerFree },
    { CSR_WIFI_SME_MIB_GET_NEXT_REQ, CsrWifiSmeMibGetNextReqSizeof, CsrWifiSmeMibGetNextReqSer, CsrWifiSmeMibGetNextReqDes, CsrWifiSmeMibGetNextReqSerFree },
    { CSR_WIFI_SME_MIB_GET_REQ, CsrWifiSmeMibGetReqSizeof, CsrWifiSmeMibGetReqSer, CsrWifiSmeMibGetReqDes, CsrWifiSmeMibGetReqSerFree },
    { CSR_WIFI_SME_MIB_SET_REQ, CsrWifiSmeMibSetReqSizeof, CsrWifiSmeMibSetReqSer, CsrWifiSmeMibSetReqDes, CsrWifiSmeMibSetReqSerFree },
    { CSR_WIFI_SME_MULTICAST_ADDRESS_REQ, CsrWifiSmeMulticastAddressReqSizeof, CsrWifiSmeMulticastAddressReqSer, CsrWifiSmeMulticastAddressReqDes, CsrWifiSmeMulticastAddressReqSerFree },
    { CSR_WIFI_SME_PACKET_FILTER_SET_REQ, CsrWifiSmePacketFilterSetReqSizeof, CsrWifiSmePacketFilterSetReqSer, CsrWifiSmePacketFilterSetReqDes, CsrWifiSmePacketFilterSetReqSerFree },
    { CSR_WIFI_SME_PERMANENT_MAC_ADDRESS_GET_REQ, CsrWifiSmePermanentMacAddressGetReqSizeof, CsrWifiSmePermanentMacAddressGetReqSer, CsrWifiSmePermanentMacAddressGetReqDes, CsrWifiSmePermanentMacAddressGetReqSerFree },
    { CSR_WIFI_SME_PMKID_REQ, CsrWifiSmePmkidReqSizeof, CsrWifiSmePmkidReqSer, CsrWifiSmePmkidReqDes, CsrWifiSmePmkidReqSerFree },
    { CSR_WIFI_SME_POWER_CONFIG_GET_REQ, CsrWifiSmePowerConfigGetReqSizeof, CsrWifiSmePowerConfigGetReqSer, CsrWifiSmePowerConfigGetReqDes, CsrWifiSmePowerConfigGetReqSerFree },
    { CSR_WIFI_SME_POWER_CONFIG_SET_REQ, CsrWifiSmePowerConfigSetReqSizeof, CsrWifiSmePowerConfigSetReqSer, CsrWifiSmePowerConfigSetReqDes, CsrWifiSmePowerConfigSetReqSerFree },
    { CSR_WIFI_SME_REGULATORY_DOMAIN_INFO_GET_REQ, CsrWifiSmeRegulatoryDomainInfoGetReqSizeof, CsrWifiSmeRegulatoryDomainInfoGetReqSer, CsrWifiSmeRegulatoryDomainInfoGetReqDes, CsrWifiSmeRegulatoryDomainInfoGetReqSerFree },
    { CSR_WIFI_SME_ROAMING_CONFIG_GET_REQ, CsrWifiSmeRoamingConfigGetReqSizeof, CsrWifiSmeRoamingConfigGetReqSer, CsrWifiSmeRoamingConfigGetReqDes, CsrWifiSmeRoamingConfigGetReqSerFree },
    { CSR_WIFI_SME_ROAMING_CONFIG_SET_REQ, CsrWifiSmeRoamingConfigSetReqSizeof, CsrWifiSmeRoamingConfigSetReqSer, CsrWifiSmeRoamingConfigSetReqDes, CsrWifiSmeRoamingConfigSetReqSerFree },
    { CSR_WIFI_SME_SCAN_CONFIG_GET_REQ, CsrWifiSmeScanConfigGetReqSizeof, CsrWifiSmeScanConfigGetReqSer, CsrWifiSmeScanConfigGetReqDes, CsrWifiSmeScanConfigGetReqSerFree },
    { CSR_WIFI_SME_SCAN_CONFIG_SET_REQ, CsrWifiSmeScanConfigSetReqSizeof, CsrWifiSmeScanConfigSetReqSer, CsrWifiSmeScanConfigSetReqDes, CsrWifiSmeScanConfigSetReqSerFree },
    { CSR_WIFI_SME_SCAN_FULL_REQ, CsrWifiSmeScanFullReqSizeof, CsrWifiSmeScanFullReqSer, CsrWifiSmeScanFullReqDes, CsrWifiSmeScanFullReqSerFree },
    { CSR_WIFI_SME_SCAN_RESULTS_FLUSH_REQ, CsrWifiSmeScanResultsFlushReqSizeof, CsrWifiSmeScanResultsFlushReqSer, CsrWifiSmeScanResultsFlushReqDes, CsrWifiSmeScanResultsFlushReqSerFree },
    { CSR_WIFI_SME_SCAN_RESULTS_GET_REQ, CsrWifiSmeScanResultsGetReqSizeof, CsrWifiSmeScanResultsGetReqSer, CsrWifiSmeScanResultsGetReqDes, CsrWifiSmeScanResultsGetReqSerFree },
    { CSR_WIFI_SME_SME_STA_CONFIG_GET_REQ, CsrWifiSmeSmeStaConfigGetReqSizeof, CsrWifiSmeSmeStaConfigGetReqSer, CsrWifiSmeSmeStaConfigGetReqDes, CsrWifiSmeSmeStaConfigGetReqSerFree },
    { CSR_WIFI_SME_SME_STA_CONFIG_SET_REQ, CsrWifiSmeSmeStaConfigSetReqSizeof, CsrWifiSmeSmeStaConfigSetReqSer, CsrWifiSmeSmeStaConfigSetReqDes, CsrWifiSmeSmeStaConfigSetReqSerFree },
    { CSR_WIFI_SME_STATION_MAC_ADDRESS_GET_REQ, CsrWifiSmeStationMacAddressGetReqSizeof, CsrWifiSmeStationMacAddressGetReqSer, CsrWifiSmeStationMacAddressGetReqDes, CsrWifiSmeStationMacAddressGetReqSerFree },
    { CSR_WIFI_SME_TSPEC_REQ, CsrWifiSmeTspecReqSizeof, CsrWifiSmeTspecReqSer, CsrWifiSmeTspecReqDes, CsrWifiSmeTspecReqSerFree },
    { CSR_WIFI_SME_VERSIONS_GET_REQ, CsrWifiSmeVersionsGetReqSizeof, CsrWifiSmeVersionsGetReqSer, CsrWifiSmeVersionsGetReqDes, CsrWifiSmeVersionsGetReqSerFree },
    { CSR_WIFI_SME_WIFI_FLIGHTMODE_REQ, CsrWifiSmeWifiFlightmodeReqSizeof, CsrWifiSmeWifiFlightmodeReqSer, CsrWifiSmeWifiFlightmodeReqDes, CsrWifiSmeWifiFlightmodeReqSerFree },
    { CSR_WIFI_SME_WIFI_OFF_REQ, CsrWifiSmeWifiOffReqSizeof, CsrWifiSmeWifiOffReqSer, CsrWifiSmeWifiOffReqDes, CsrWifiSmeWifiOffReqSerFree },
    { CSR_WIFI_SME_WIFI_ON_REQ, CsrWifiSmeWifiOnReqSizeof, CsrWifiSmeWifiOnReqSer, CsrWifiSmeWifiOnReqDes, CsrWifiSmeWifiOnReqSerFree },
    { CSR_WIFI_SME_CLOAKED_SSIDS_SET_REQ, CsrWifiSmeCloakedSsidsSetReqSizeof, CsrWifiSmeCloakedSsidsSetReqSer, CsrWifiSmeCloakedSsidsSetReqDes, CsrWifiSmeCloakedSsidsSetReqSerFree },
    { CSR_WIFI_SME_CLOAKED_SSIDS_GET_REQ, CsrWifiSmeCloakedSsidsGetReqSizeof, CsrWifiSmeCloakedSsidsGetReqSer, CsrWifiSmeCloakedSsidsGetReqDes, CsrWifiSmeCloakedSsidsGetReqSerFree },
    { CSR_WIFI_SME_SME_COMMON_CONFIG_GET_REQ, CsrWifiSmeSmeCommonConfigGetReqSizeof, CsrWifiSmeSmeCommonConfigGetReqSer, CsrWifiSmeSmeCommonConfigGetReqDes, CsrWifiSmeSmeCommonConfigGetReqSerFree },
    { CSR_WIFI_SME_SME_COMMON_CONFIG_SET_REQ, CsrWifiSmeSmeCommonConfigSetReqSizeof, CsrWifiSmeSmeCommonConfigSetReqSer, CsrWifiSmeSmeCommonConfigSetReqDes, CsrWifiSmeSmeCommonConfigSetReqSerFree },
    { CSR_WIFI_SME_INTERFACE_CAPABILITY_GET_REQ, CsrWifiSmeInterfaceCapabilityGetReqSizeof, CsrWifiSmeInterfaceCapabilityGetReqSer, CsrWifiSmeInterfaceCapabilityGetReqDes, CsrWifiSmeInterfaceCapabilityGetReqSerFree },
    { CSR_WIFI_SME_WPS_CONFIGURATION_REQ, CsrWifiSmeWpsConfigurationReqSizeof, CsrWifiSmeWpsConfigurationReqSer, CsrWifiSmeWpsConfigurationReqDes, CsrWifiSmeWpsConfigurationReqSerFree },
    { CSR_WIFI_SME_SET_REQ, CsrWifiSmeSetReqSizeof, CsrWifiSmeSetReqSer, CsrWifiSmeSetReqDes, CsrWifiSmeSetReqSerFree },
    { CSR_WIFI_SME_ACTIVATE_CFM, CsrWifiSmeActivateCfmSizeof, CsrWifiSmeActivateCfmSer, CsrWifiSmeActivateCfmDes, CsrWifiSmeActivateCfmSerFree },
    { CSR_WIFI_SME_ADHOC_CONFIG_GET_CFM, CsrWifiSmeAdhocConfigGetCfmSizeof, CsrWifiSmeAdhocConfigGetCfmSer, CsrWifiSmeAdhocConfigGetCfmDes, CsrWifiSmeAdhocConfigGetCfmSerFree },
    { CSR_WIFI_SME_ADHOC_CONFIG_SET_CFM, CsrWifiSmeAdhocConfigSetCfmSizeof, CsrWifiSmeAdhocConfigSetCfmSer, CsrWifiSmeAdhocConfigSetCfmDes, CsrWifiSmeAdhocConfigSetCfmSerFree },
    { CSR_WIFI_SME_ASSOCIATION_COMPLETE_IND, CsrWifiSmeAssociationCompleteIndSizeof, CsrWifiSmeAssociationCompleteIndSer, CsrWifiSmeAssociationCompleteIndDes, CsrWifiSmeAssociationCompleteIndSerFree },
    { CSR_WIFI_SME_ASSOCIATION_START_IND, CsrWifiSmeAssociationStartIndSizeof, CsrWifiSmeAssociationStartIndSer, CsrWifiSmeAssociationStartIndDes, CsrWifiSmeAssociationStartIndSerFree },
    { CSR_WIFI_SME_BLACKLIST_CFM, CsrWifiSmeBlacklistCfmSizeof, CsrWifiSmeBlacklistCfmSer, CsrWifiSmeBlacklistCfmDes, CsrWifiSmeBlacklistCfmSerFree },
    { CSR_WIFI_SME_CALIBRATION_DATA_GET_CFM, CsrWifiSmeCalibrationDataGetCfmSizeof, CsrWifiSmeCalibrationDataGetCfmSer, CsrWifiSmeCalibrationDataGetCfmDes, CsrWifiSmeCalibrationDataGetCfmSerFree },
    { CSR_WIFI_SME_CALIBRATION_DATA_SET_CFM, CsrWifiSmeCalibrationDataSetCfmSizeof, CsrWifiSmeCalibrationDataSetCfmSer, CsrWifiSmeCalibrationDataSetCfmDes, CsrWifiSmeCalibrationDataSetCfmSerFree },
    { CSR_WIFI_SME_CCX_CONFIG_GET_CFM, CsrWifiSmeCcxConfigGetCfmSizeof, CsrWifiSmeCcxConfigGetCfmSer, CsrWifiSmeCcxConfigGetCfmDes, CsrWifiSmeCcxConfigGetCfmSerFree },
    { CSR_WIFI_SME_CCX_CONFIG_SET_CFM, CsrWifiSmeCcxConfigSetCfmSizeof, CsrWifiSmeCcxConfigSetCfmSer, CsrWifiSmeCcxConfigSetCfmDes, CsrWifiSmeCcxConfigSetCfmSerFree },
    { CSR_WIFI_SME_COEX_CONFIG_GET_CFM, CsrWifiSmeCoexConfigGetCfmSizeof, CsrWifiSmeCoexConfigGetCfmSer, CsrWifiSmeCoexConfigGetCfmDes, CsrWifiSmeCoexConfigGetCfmSerFree },
    { CSR_WIFI_SME_COEX_CONFIG_SET_CFM, CsrWifiSmeCoexConfigSetCfmSizeof, CsrWifiSmeCoexConfigSetCfmSer, CsrWifiSmeCoexConfigSetCfmDes, CsrWifiSmeCoexConfigSetCfmSerFree },
    { CSR_WIFI_SME_COEX_INFO_GET_CFM, CsrWifiSmeCoexInfoGetCfmSizeof, CsrWifiSmeCoexInfoGetCfmSer, CsrWifiSmeCoexInfoGetCfmDes, CsrWifiSmeCoexInfoGetCfmSerFree },
    { CSR_WIFI_SME_CONNECT_CFM, CsrWifiSmeConnectCfmSizeof, CsrWifiSmeConnectCfmSer, CsrWifiSmeConnectCfmDes, CsrWifiSmeConnectCfmSerFree },
    { CSR_WIFI_SME_CONNECTION_CONFIG_GET_CFM, CsrWifiSmeConnectionConfigGetCfmSizeof, CsrWifiSmeConnectionConfigGetCfmSer, CsrWifiSmeConnectionConfigGetCfmDes, CsrWifiSmeConnectionConfigGetCfmSerFree },
    { CSR_WIFI_SME_CONNECTION_INFO_GET_CFM, CsrWifiSmeConnectionInfoGetCfmSizeof, CsrWifiSmeConnectionInfoGetCfmSer, CsrWifiSmeConnectionInfoGetCfmDes, CsrWifiSmeConnectionInfoGetCfmSerFree },
    { CSR_WIFI_SME_CONNECTION_QUALITY_IND, CsrWifiSmeConnectionQualityIndSizeof, CsrWifiSmeConnectionQualityIndSer, CsrWifiSmeConnectionQualityIndDes, CsrWifiSmeConnectionQualityIndSerFree },
    { CSR_WIFI_SME_CONNECTION_STATS_GET_CFM, CsrWifiSmeConnectionStatsGetCfmSizeof, CsrWifiSmeConnectionStatsGetCfmSer, CsrWifiSmeConnectionStatsGetCfmDes, CsrWifiSmeConnectionStatsGetCfmSerFree },
    { CSR_WIFI_SME_DEACTIVATE_CFM, CsrWifiSmeDeactivateCfmSizeof, CsrWifiSmeDeactivateCfmSer, CsrWifiSmeDeactivateCfmDes, CsrWifiSmeDeactivateCfmSerFree },
    { CSR_WIFI_SME_DISCONNECT_CFM, CsrWifiSmeDisconnectCfmSizeof, CsrWifiSmeDisconnectCfmSer, CsrWifiSmeDisconnectCfmDes, CsrWifiSmeDisconnectCfmSerFree },
    { CSR_WIFI_SME_EVENT_MASK_SET_CFM, CsrWifiSmeEventMaskSetCfmSizeof, CsrWifiSmeEventMaskSetCfmSer, CsrWifiSmeEventMaskSetCfmDes, CsrWifiSmeEventMaskSetCfmSerFree },
    { CSR_WIFI_SME_HOST_CONFIG_GET_CFM, CsrWifiSmeHostConfigGetCfmSizeof, CsrWifiSmeHostConfigGetCfmSer, CsrWifiSmeHostConfigGetCfmDes, CsrWifiSmeHostConfigGetCfmSerFree },
    { CSR_WIFI_SME_HOST_CONFIG_SET_CFM, CsrWifiSmeHostConfigSetCfmSizeof, CsrWifiSmeHostConfigSetCfmSer, CsrWifiSmeHostConfigSetCfmDes, CsrWifiSmeHostConfigSetCfmSerFree },
    { CSR_WIFI_SME_IBSS_STATION_IND, CsrWifiSmeIbssStationIndSizeof, CsrWifiSmeIbssStationIndSer, CsrWifiSmeIbssStationIndDes, CsrWifiSmeIbssStationIndSerFree },
    { CSR_WIFI_SME_KEY_CFM, CsrWifiSmeKeyCfmSizeof, CsrWifiSmeKeyCfmSer, CsrWifiSmeKeyCfmDes, CsrWifiSmeKeyCfmSerFree },
    { CSR_WIFI_SME_LINK_QUALITY_GET_CFM, CsrWifiSmeLinkQualityGetCfmSizeof, CsrWifiSmeLinkQualityGetCfmSer, CsrWifiSmeLinkQualityGetCfmDes, CsrWifiSmeLinkQualityGetCfmSerFree },
    { CSR_WIFI_SME_MEDIA_STATUS_IND, CsrWifiSmeMediaStatusIndSizeof, CsrWifiSmeMediaStatusIndSer, CsrWifiSmeMediaStatusIndDes, CsrWifiSmeMediaStatusIndSerFree },
    { CSR_WIFI_SME_MIB_CONFIG_GET_CFM, CsrWifiSmeMibConfigGetCfmSizeof, CsrWifiSmeMibConfigGetCfmSer, CsrWifiSmeMibConfigGetCfmDes, CsrWifiSmeMibConfigGetCfmSerFree },
    { CSR_WIFI_SME_MIB_CONFIG_SET_CFM, CsrWifiSmeMibConfigSetCfmSizeof, CsrWifiSmeMibConfigSetCfmSer, CsrWifiSmeMibConfigSetCfmDes, CsrWifiSmeMibConfigSetCfmSerFree },
    { CSR_WIFI_SME_MIB_GET_CFM, CsrWifiSmeMibGetCfmSizeof, CsrWifiSmeMibGetCfmSer, CsrWifiSmeMibGetCfmDes, CsrWifiSmeMibGetCfmSerFree },
    { CSR_WIFI_SME_MIB_GET_NEXT_CFM, CsrWifiSmeMibGetNextCfmSizeof, CsrWifiSmeMibGetNextCfmSer, CsrWifiSmeMibGetNextCfmDes, CsrWifiSmeMibGetNextCfmSerFree },
    { CSR_WIFI_SME_MIB_SET_CFM, CsrWifiSmeMibSetCfmSizeof, CsrWifiSmeMibSetCfmSer, CsrWifiSmeMibSetCfmDes, CsrWifiSmeMibSetCfmSerFree },
    { CSR_WIFI_SME_MIC_FAILURE_IND, CsrWifiSmeMicFailureIndSizeof, CsrWifiSmeMicFailureIndSer, CsrWifiSmeMicFailureIndDes, CsrWifiSmeMicFailureIndSerFree },
    { CSR_WIFI_SME_MULTICAST_ADDRESS_CFM, CsrWifiSmeMulticastAddressCfmSizeof, CsrWifiSmeMulticastAddressCfmSer, CsrWifiSmeMulticastAddressCfmDes, CsrWifiSmeMulticastAddressCfmSerFree },
    { CSR_WIFI_SME_PACKET_FILTER_SET_CFM, CsrWifiSmePacketFilterSetCfmSizeof, CsrWifiSmePacketFilterSetCfmSer, CsrWifiSmePacketFilterSetCfmDes, CsrWifiSmePacketFilterSetCfmSerFree },
    { CSR_WIFI_SME_PERMANENT_MAC_ADDRESS_GET_CFM, CsrWifiSmePermanentMacAddressGetCfmSizeof, CsrWifiSmePermanentMacAddressGetCfmSer, CsrWifiSmePermanentMacAddressGetCfmDes, CsrWifiSmePermanentMacAddressGetCfmSerFree },
    { CSR_WIFI_SME_PMKID_CANDIDATE_LIST_IND, CsrWifiSmePmkidCandidateListIndSizeof, CsrWifiSmePmkidCandidateListIndSer, CsrWifiSmePmkidCandidateListIndDes, CsrWifiSmePmkidCandidateListIndSerFree },
    { CSR_WIFI_SME_PMKID_CFM, CsrWifiSmePmkidCfmSizeof, CsrWifiSmePmkidCfmSer, CsrWifiSmePmkidCfmDes, CsrWifiSmePmkidCfmSerFree },
    { CSR_WIFI_SME_POWER_CONFIG_GET_CFM, CsrWifiSmePowerConfigGetCfmSizeof, CsrWifiSmePowerConfigGetCfmSer, CsrWifiSmePowerConfigGetCfmDes, CsrWifiSmePowerConfigGetCfmSerFree },
    { CSR_WIFI_SME_POWER_CONFIG_SET_CFM, CsrWifiSmePowerConfigSetCfmSizeof, CsrWifiSmePowerConfigSetCfmSer, CsrWifiSmePowerConfigSetCfmDes, CsrWifiSmePowerConfigSetCfmSerFree },
    { CSR_WIFI_SME_REGULATORY_DOMAIN_INFO_GET_CFM, CsrWifiSmeRegulatoryDomainInfoGetCfmSizeof, CsrWifiSmeRegulatoryDomainInfoGetCfmSer, CsrWifiSmeRegulatoryDomainInfoGetCfmDes, CsrWifiSmeRegulatoryDomainInfoGetCfmSerFree },
    { CSR_WIFI_SME_ROAM_COMPLETE_IND, CsrWifiSmeRoamCompleteIndSizeof, CsrWifiSmeRoamCompleteIndSer, CsrWifiSmeRoamCompleteIndDes, CsrWifiSmeRoamCompleteIndSerFree },
    { CSR_WIFI_SME_ROAM_START_IND, CsrWifiSmeRoamStartIndSizeof, CsrWifiSmeRoamStartIndSer, CsrWifiSmeRoamStartIndDes, CsrWifiSmeRoamStartIndSerFree },
    { CSR_WIFI_SME_ROAMING_CONFIG_GET_CFM, CsrWifiSmeRoamingConfigGetCfmSizeof, CsrWifiSmeRoamingConfigGetCfmSer, CsrWifiSmeRoamingConfigGetCfmDes, CsrWifiSmeRoamingConfigGetCfmSerFree },
    { CSR_WIFI_SME_ROAMING_CONFIG_SET_CFM, CsrWifiSmeRoamingConfigSetCfmSizeof, CsrWifiSmeRoamingConfigSetCfmSer, CsrWifiSmeRoamingConfigSetCfmDes, CsrWifiSmeRoamingConfigSetCfmSerFree },
    { CSR_WIFI_SME_SCAN_CONFIG_GET_CFM, CsrWifiSmeScanConfigGetCfmSizeof, CsrWifiSmeScanConfigGetCfmSer, CsrWifiSmeScanConfigGetCfmDes, CsrWifiSmeScanConfigGetCfmSerFree },
    { CSR_WIFI_SME_SCAN_CONFIG_SET_CFM, CsrWifiSmeScanConfigSetCfmSizeof, CsrWifiSmeScanConfigSetCfmSer, CsrWifiSmeScanConfigSetCfmDes, CsrWifiSmeScanConfigSetCfmSerFree },
    { CSR_WIFI_SME_SCAN_FULL_CFM, CsrWifiSmeScanFullCfmSizeof, CsrWifiSmeScanFullCfmSer, CsrWifiSmeScanFullCfmDes, CsrWifiSmeScanFullCfmSerFree },
    { CSR_WIFI_SME_SCAN_RESULT_IND, CsrWifiSmeScanResultIndSizeof, CsrWifiSmeScanResultIndSer, CsrWifiSmeScanResultIndDes, CsrWifiSmeScanResultIndSerFree },
    { CSR_WIFI_SME_SCAN_RESULTS_FLUSH_CFM, CsrWifiSmeScanResultsFlushCfmSizeof, CsrWifiSmeScanResultsFlushCfmSer, CsrWifiSmeScanResultsFlushCfmDes, CsrWifiSmeScanResultsFlushCfmSerFree },
    { CSR_WIFI_SME_SCAN_RESULTS_GET_CFM, CsrWifiSmeScanResultsGetCfmSizeof, CsrWifiSmeScanResultsGetCfmSer, CsrWifiSmeScanResultsGetCfmDes, CsrWifiSmeScanResultsGetCfmSerFree },
    { CSR_WIFI_SME_SME_STA_CONFIG_GET_CFM, CsrWifiSmeSmeStaConfigGetCfmSizeof, CsrWifiSmeSmeStaConfigGetCfmSer, CsrWifiSmeSmeStaConfigGetCfmDes, CsrWifiSmeSmeStaConfigGetCfmSerFree },
    { CSR_WIFI_SME_SME_STA_CONFIG_SET_CFM, CsrWifiSmeSmeStaConfigSetCfmSizeof, CsrWifiSmeSmeStaConfigSetCfmSer, CsrWifiSmeSmeStaConfigSetCfmDes, CsrWifiSmeSmeStaConfigSetCfmSerFree },
    { CSR_WIFI_SME_STATION_MAC_ADDRESS_GET_CFM, CsrWifiSmeStationMacAddressGetCfmSizeof, CsrWifiSmeStationMacAddressGetCfmSer, CsrWifiSmeStationMacAddressGetCfmDes, CsrWifiSmeStationMacAddressGetCfmSerFree },
    { CSR_WIFI_SME_TSPEC_IND, CsrWifiSmeTspecIndSizeof, CsrWifiSmeTspecIndSer, CsrWifiSmeTspecIndDes, CsrWifiSmeTspecIndSerFree },
    { CSR_WIFI_SME_TSPEC_CFM, CsrWifiSmeTspecCfmSizeof, CsrWifiSmeTspecCfmSer, CsrWifiSmeTspecCfmDes, CsrWifiSmeTspecCfmSerFree },
    { CSR_WIFI_SME_VERSIONS_GET_CFM, CsrWifiSmeVersionsGetCfmSizeof, CsrWifiSmeVersionsGetCfmSer, CsrWifiSmeVersionsGetCfmDes, CsrWifiSmeVersionsGetCfmSerFree },
    { CSR_WIFI_SME_WIFI_FLIGHTMODE_CFM, CsrWifiSmeWifiFlightmodeCfmSizeof, CsrWifiSmeWifiFlightmodeCfmSer, CsrWifiSmeWifiFlightmodeCfmDes, CsrWifiSmeWifiFlightmodeCfmSerFree },
    { CSR_WIFI_SME_WIFI_OFF_IND, CsrWifiSmeWifiOffIndSizeof, CsrWifiSmeWifiOffIndSer, CsrWifiSmeWifiOffIndDes, CsrWifiSmeWifiOffIndSerFree },
    { CSR_WIFI_SME_WIFI_OFF_CFM, CsrWifiSmeWifiOffCfmSizeof, CsrWifiSmeWifiOffCfmSer, CsrWifiSmeWifiOffCfmDes, CsrWifiSmeWifiOffCfmSerFree },
    { CSR_WIFI_SME_WIFI_ON_CFM, CsrWifiSmeWifiOnCfmSizeof, CsrWifiSmeWifiOnCfmSer, CsrWifiSmeWifiOnCfmDes, CsrWifiSmeWifiOnCfmSerFree },
    { CSR_WIFI_SME_CLOAKED_SSIDS_SET_CFM, CsrWifiSmeCloakedSsidsSetCfmSizeof, CsrWifiSmeCloakedSsidsSetCfmSer, CsrWifiSmeCloakedSsidsSetCfmDes, CsrWifiSmeCloakedSsidsSetCfmSerFree },
    { CSR_WIFI_SME_CLOAKED_SSIDS_GET_CFM, CsrWifiSmeCloakedSsidsGetCfmSizeof, CsrWifiSmeCloakedSsidsGetCfmSer, CsrWifiSmeCloakedSsidsGetCfmDes, CsrWifiSmeCloakedSsidsGetCfmSerFree },
    { CSR_WIFI_SME_WIFI_ON_IND, CsrWifiSmeWifiOnIndSizeof, CsrWifiSmeWifiOnIndSer, CsrWifiSmeWifiOnIndDes, CsrWifiSmeWifiOnIndSerFree },
    { CSR_WIFI_SME_SME_COMMON_CONFIG_GET_CFM, CsrWifiSmeSmeCommonConfigGetCfmSizeof, CsrWifiSmeSmeCommonConfigGetCfmSer, CsrWifiSmeSmeCommonConfigGetCfmDes, CsrWifiSmeSmeCommonConfigGetCfmSerFree },
    { CSR_WIFI_SME_SME_COMMON_CONFIG_SET_CFM, CsrWifiSmeSmeCommonConfigSetCfmSizeof, CsrWifiSmeSmeCommonConfigSetCfmSer, CsrWifiSmeSmeCommonConfigSetCfmDes, CsrWifiSmeSmeCommonConfigSetCfmSerFree },
    { CSR_WIFI_SME_INTERFACE_CAPABILITY_GET_CFM, CsrWifiSmeInterfaceCapabilityGetCfmSizeof, CsrWifiSmeInterfaceCapabilityGetCfmSer, CsrWifiSmeInterfaceCapabilityGetCfmDes, CsrWifiSmeInterfaceCapabilityGetCfmSerFree },
    { CSR_WIFI_SME_ERROR_IND, CsrWifiSmeErrorIndSizeof, CsrWifiSmeErrorIndSer, CsrWifiSmeErrorIndDes, CsrWifiSmeErrorIndSerFree },
    { CSR_WIFI_SME_INFO_IND, CsrWifiSmeInfoIndSizeof, CsrWifiSmeInfoIndSer, CsrWifiSmeInfoIndDes, CsrWifiSmeInfoIndSerFree },
    { CSR_WIFI_SME_CORE_DUMP_IND, CsrWifiSmeCoreDumpIndSizeof, CsrWifiSmeCoreDumpIndSer, CsrWifiSmeCoreDumpIndDes, CsrWifiSmeCoreDumpIndSerFree },
    { CSR_WIFI_SME_AMP_STATUS_CHANGE_IND, CsrWifiSmeAmpStatusChangeIndSizeof, CsrWifiSmeAmpStatusChangeIndSer, CsrWifiSmeAmpStatusChangeIndDes, CsrWifiSmeAmpStatusChangeIndSerFree },
    { CSR_WIFI_SME_WPS_CONFIGURATION_CFM, CsrWifiSmeWpsConfigurationCfmSizeof, CsrWifiSmeWpsConfigurationCfmSer, CsrWifiSmeWpsConfigurationCfmDes, CsrWifiSmeWpsConfigurationCfmSerFree },

    { 0, NULL, NULL, NULL, NULL },
};

CsrMsgConvMsgEntry* CsrWifiSmeConverterLookup(CsrMsgConvMsgEntry *ce, u16 msgType)
{
    if (msgType & CSR_PRIM_UPSTREAM)
    {
        u16 idx = (msgType & ~CSR_PRIM_UPSTREAM) + CSR_WIFI_SME_PRIM_DOWNSTREAM_COUNT;
        if (idx < (CSR_WIFI_SME_PRIM_UPSTREAM_COUNT + CSR_WIFI_SME_PRIM_DOWNSTREAM_COUNT) &&
            csrwifisme_conv_lut[idx].msgType == msgType)
        {
            return &csrwifisme_conv_lut[idx];
        }
    }
    else
    {
        if (msgType < CSR_WIFI_SME_PRIM_DOWNSTREAM_COUNT &&
            csrwifisme_conv_lut[msgType].msgType == msgType)
        {
            return &csrwifisme_conv_lut[msgType];
        }
    }
    return NULL;
}


void CsrWifiSmeConverterInit(void)
{
    CsrMsgConvInsert(CSR_WIFI_SME_PRIM, csrwifisme_conv_lut);
    CsrMsgConvCustomLookupRegister(CSR_WIFI_SME_PRIM, CsrWifiSmeConverterLookup);
}


#ifdef CSR_LOG_ENABLE
static const CsrLogPrimitiveInformation csrwifisme_conv_info = {
    CSR_WIFI_SME_PRIM,
    (char *)"CSR_WIFI_SME_PRIM",
    csrwifisme_conv_lut
};
const CsrLogPrimitiveInformation* CsrWifiSmeTechInfoGet(void)
{
    return &csrwifisme_conv_info;
}


#endif /* CSR_LOG_ENABLE */
#endif /* EXCLUDE_CSR_WIFI_SME_MODULE */
