/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#include "csr_msgconv.h"
#include "csr_pmem.h"
#include "csr_macro.h"

#ifdef CSR_LOG_ENABLE
#include "csr_log.h"
#endif

#ifndef EXCLUDE_CSR_WIFI_ROUTER_CTRL_MODULE
#include "csr_wifi_router_ctrl_serialize.h"
#include "csr_wifi_router_ctrl_prim.h"

static CsrMsgConvMsgEntry csrwifirouterctrl_conv_lut[] = {
    { CSR_WIFI_ROUTER_CTRL_CONFIGURE_POWER_MODE_REQ, CsrWifiRouterCtrlConfigurePowerModeReqSizeof, CsrWifiRouterCtrlConfigurePowerModeReqSer, CsrWifiRouterCtrlConfigurePowerModeReqDes, CsrWifiRouterCtrlConfigurePowerModeReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_HIP_REQ, CsrWifiRouterCtrlHipReqSizeof, CsrWifiRouterCtrlHipReqSer, CsrWifiRouterCtrlHipReqDes, CsrWifiRouterCtrlHipReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_MEDIA_STATUS_REQ, CsrWifiRouterCtrlMediaStatusReqSizeof, CsrWifiRouterCtrlMediaStatusReqSer, CsrWifiRouterCtrlMediaStatusReqDes, CsrWifiRouterCtrlMediaStatusReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_MULTICAST_ADDRESS_RES, CsrWifiRouterCtrlMulticastAddressResSizeof, CsrWifiRouterCtrlMulticastAddressResSer, CsrWifiRouterCtrlMulticastAddressResDes, CsrWifiRouterCtrlMulticastAddressResSerFree },
    { CSR_WIFI_ROUTER_CTRL_PORT_CONFIGURE_REQ, CsrWifiRouterCtrlPortConfigureReqSizeof, CsrWifiRouterCtrlPortConfigureReqSer, CsrWifiRouterCtrlPortConfigureReqDes, CsrWifiRouterCtrlPortConfigureReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_QOS_CONTROL_REQ, CsrWifiRouterCtrlQosControlReqSizeof, CsrWifiRouterCtrlQosControlReqSer, CsrWifiRouterCtrlQosControlReqDes, CsrWifiRouterCtrlQosControlReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_SUSPEND_RES, CsrWifiRouterCtrlSuspendResSizeof, CsrWifiRouterCtrlSuspendResSer, CsrWifiRouterCtrlSuspendResDes, CsrWifiRouterCtrlSuspendResSerFree },
    { CSR_WIFI_ROUTER_CTRL_TCLAS_ADD_REQ, CsrWifiRouterCtrlTclasAddReqSizeof, CsrWifiRouterCtrlTclasAddReqSer, CsrWifiRouterCtrlTclasAddReqDes, CsrWifiRouterCtrlTclasAddReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_RESUME_RES, CsrWifiRouterCtrlResumeResSizeof, CsrWifiRouterCtrlResumeResSer, CsrWifiRouterCtrlResumeResDes, CsrWifiRouterCtrlResumeResSerFree },
    { CSR_WIFI_ROUTER_CTRL_RAW_SDIO_DEINITIALISE_REQ, CsrWifiRouterCtrlRawSdioDeinitialiseReqSizeof, CsrWifiRouterCtrlRawSdioDeinitialiseReqSer, CsrWifiRouterCtrlRawSdioDeinitialiseReqDes, CsrWifiRouterCtrlRawSdioDeinitialiseReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_RAW_SDIO_INITIALISE_REQ, CsrWifiRouterCtrlRawSdioInitialiseReqSizeof, CsrWifiRouterCtrlRawSdioInitialiseReqSer, CsrWifiRouterCtrlRawSdioInitialiseReqDes, CsrWifiRouterCtrlRawSdioInitialiseReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_TCLAS_DEL_REQ, CsrWifiRouterCtrlTclasDelReqSizeof, CsrWifiRouterCtrlTclasDelReqSer, CsrWifiRouterCtrlTclasDelReqDes, CsrWifiRouterCtrlTclasDelReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_TRAFFIC_CLASSIFICATION_REQ, CsrWifiRouterCtrlTrafficClassificationReqSizeof, CsrWifiRouterCtrlTrafficClassificationReqSer, CsrWifiRouterCtrlTrafficClassificationReqDes, CsrWifiRouterCtrlTrafficClassificationReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_TRAFFIC_CONFIG_REQ, CsrWifiRouterCtrlTrafficConfigReqSizeof, CsrWifiRouterCtrlTrafficConfigReqSer, CsrWifiRouterCtrlTrafficConfigReqDes, CsrWifiRouterCtrlTrafficConfigReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_WIFI_OFF_REQ, CsrWifiRouterCtrlWifiOffReqSizeof, CsrWifiRouterCtrlWifiOffReqSer, CsrWifiRouterCtrlWifiOffReqDes, CsrWifiRouterCtrlWifiOffReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_WIFI_OFF_RES, CsrWifiRouterCtrlWifiOffResSizeof, CsrWifiRouterCtrlWifiOffResSer, CsrWifiRouterCtrlWifiOffResDes, CsrWifiRouterCtrlWifiOffResSerFree },
    { CSR_WIFI_ROUTER_CTRL_WIFI_ON_REQ, CsrWifiRouterCtrlWifiOnReqSizeof, CsrWifiRouterCtrlWifiOnReqSer, CsrWifiRouterCtrlWifiOnReqDes, CsrWifiRouterCtrlWifiOnReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_WIFI_ON_RES, CsrWifiRouterCtrlWifiOnResSizeof, CsrWifiRouterCtrlWifiOnResSer, CsrWifiRouterCtrlWifiOnResDes, CsrWifiRouterCtrlWifiOnResSerFree },
    { CSR_WIFI_ROUTER_CTRL_M4_TRANSMIT_REQ, CsrWifiRouterCtrlM4TransmitReqSizeof, CsrWifiRouterCtrlM4TransmitReqSer, CsrWifiRouterCtrlM4TransmitReqDes, CsrWifiRouterCtrlM4TransmitReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_MODE_SET_REQ, CsrWifiRouterCtrlModeSetReqSizeof, CsrWifiRouterCtrlModeSetReqSer, CsrWifiRouterCtrlModeSetReqDes, CsrWifiRouterCtrlModeSetReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_PEER_ADD_REQ, CsrWifiRouterCtrlPeerAddReqSizeof, CsrWifiRouterCtrlPeerAddReqSer, CsrWifiRouterCtrlPeerAddReqDes, CsrWifiRouterCtrlPeerAddReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_PEER_DEL_REQ, CsrWifiRouterCtrlPeerDelReqSizeof, CsrWifiRouterCtrlPeerDelReqSer, CsrWifiRouterCtrlPeerDelReqDes, CsrWifiRouterCtrlPeerDelReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_PEER_UPDATE_REQ, CsrWifiRouterCtrlPeerUpdateReqSizeof, CsrWifiRouterCtrlPeerUpdateReqSer, CsrWifiRouterCtrlPeerUpdateReqDes, CsrWifiRouterCtrlPeerUpdateReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_CAPABILITIES_REQ, CsrWifiRouterCtrlCapabilitiesReqSizeof, CsrWifiRouterCtrlCapabilitiesReqSer, CsrWifiRouterCtrlCapabilitiesReqDes, CsrWifiRouterCtrlCapabilitiesReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_ENABLE_REQ, CsrWifiRouterCtrlBlockAckEnableReqSizeof, CsrWifiRouterCtrlBlockAckEnableReqSer, CsrWifiRouterCtrlBlockAckEnableReqDes, CsrWifiRouterCtrlBlockAckEnableReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_DISABLE_REQ, CsrWifiRouterCtrlBlockAckDisableReqSizeof, CsrWifiRouterCtrlBlockAckDisableReqSer, CsrWifiRouterCtrlBlockAckDisableReqDes, CsrWifiRouterCtrlBlockAckDisableReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_WAPI_RX_PKT_REQ, CsrWifiRouterCtrlWapiRxPktReqSizeof, CsrWifiRouterCtrlWapiRxPktReqSer, CsrWifiRouterCtrlWapiRxPktReqDes, CsrWifiRouterCtrlWapiRxPktReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_WAPI_MULTICAST_FILTER_REQ, CsrWifiRouterCtrlWapiMulticastFilterReqSizeof, CsrWifiRouterCtrlWapiMulticastFilterReqSer, CsrWifiRouterCtrlWapiMulticastFilterReqDes, CsrWifiRouterCtrlWapiMulticastFilterReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_WAPI_UNICAST_FILTER_REQ, CsrWifiRouterCtrlWapiUnicastFilterReqSizeof, CsrWifiRouterCtrlWapiUnicastFilterReqSer, CsrWifiRouterCtrlWapiUnicastFilterReqDes, CsrWifiRouterCtrlWapiUnicastFilterReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_WAPI_UNICAST_TX_PKT_REQ, CsrWifiRouterCtrlWapiUnicastTxPktReqSizeof, CsrWifiRouterCtrlWapiUnicastTxPktReqSer, CsrWifiRouterCtrlWapiUnicastTxPktReqDes, CsrWifiRouterCtrlWapiUnicastTxPktReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_WAPI_FILTER_REQ, CsrWifiRouterCtrlWapiFilterReqSizeof, CsrWifiRouterCtrlWapiFilterReqSer, CsrWifiRouterCtrlWapiFilterReqDes, CsrWifiRouterCtrlWapiFilterReqSerFree },
    { CSR_WIFI_ROUTER_CTRL_HIP_IND, CsrWifiRouterCtrlHipIndSizeof, CsrWifiRouterCtrlHipIndSer, CsrWifiRouterCtrlHipIndDes, CsrWifiRouterCtrlHipIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_MULTICAST_ADDRESS_IND, CsrWifiRouterCtrlMulticastAddressIndSizeof, CsrWifiRouterCtrlMulticastAddressIndSer, CsrWifiRouterCtrlMulticastAddressIndDes, CsrWifiRouterCtrlMulticastAddressIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_PORT_CONFIGURE_CFM, CsrWifiRouterCtrlPortConfigureCfmSizeof, CsrWifiRouterCtrlPortConfigureCfmSer, CsrWifiRouterCtrlPortConfigureCfmDes, CsrWifiRouterCtrlPortConfigureCfmSerFree },
    { CSR_WIFI_ROUTER_CTRL_RESUME_IND, CsrWifiRouterCtrlResumeIndSizeof, CsrWifiRouterCtrlResumeIndSer, CsrWifiRouterCtrlResumeIndDes, CsrWifiRouterCtrlResumeIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_SUSPEND_IND, CsrWifiRouterCtrlSuspendIndSizeof, CsrWifiRouterCtrlSuspendIndSer, CsrWifiRouterCtrlSuspendIndDes, CsrWifiRouterCtrlSuspendIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_TCLAS_ADD_CFM, CsrWifiRouterCtrlTclasAddCfmSizeof, CsrWifiRouterCtrlTclasAddCfmSer, CsrWifiRouterCtrlTclasAddCfmDes, CsrWifiRouterCtrlTclasAddCfmSerFree },
    { CSR_WIFI_ROUTER_CTRL_RAW_SDIO_DEINITIALISE_CFM, CsrWifiRouterCtrlRawSdioDeinitialiseCfmSizeof, CsrWifiRouterCtrlRawSdioDeinitialiseCfmSer, CsrWifiRouterCtrlRawSdioDeinitialiseCfmDes, CsrWifiRouterCtrlRawSdioDeinitialiseCfmSerFree },
    { CSR_WIFI_ROUTER_CTRL_RAW_SDIO_INITIALISE_CFM, CsrWifiRouterCtrlRawSdioInitialiseCfmSizeof, CsrWifiRouterCtrlRawSdioInitialiseCfmSer, CsrWifiRouterCtrlRawSdioInitialiseCfmDes, CsrWifiRouterCtrlRawSdioInitialiseCfmSerFree },
    { CSR_WIFI_ROUTER_CTRL_TCLAS_DEL_CFM, CsrWifiRouterCtrlTclasDelCfmSizeof, CsrWifiRouterCtrlTclasDelCfmSer, CsrWifiRouterCtrlTclasDelCfmDes, CsrWifiRouterCtrlTclasDelCfmSerFree },
    { CSR_WIFI_ROUTER_CTRL_TRAFFIC_PROTOCOL_IND, CsrWifiRouterCtrlTrafficProtocolIndSizeof, CsrWifiRouterCtrlTrafficProtocolIndSer, CsrWifiRouterCtrlTrafficProtocolIndDes, CsrWifiRouterCtrlTrafficProtocolIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_TRAFFIC_SAMPLE_IND, CsrWifiRouterCtrlTrafficSampleIndSizeof, CsrWifiRouterCtrlTrafficSampleIndSer, CsrWifiRouterCtrlTrafficSampleIndDes, CsrWifiRouterCtrlTrafficSampleIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_WIFI_OFF_IND, CsrWifiRouterCtrlWifiOffIndSizeof, CsrWifiRouterCtrlWifiOffIndSer, CsrWifiRouterCtrlWifiOffIndDes, CsrWifiRouterCtrlWifiOffIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_WIFI_OFF_CFM, CsrWifiRouterCtrlWifiOffCfmSizeof, CsrWifiRouterCtrlWifiOffCfmSer, CsrWifiRouterCtrlWifiOffCfmDes, CsrWifiRouterCtrlWifiOffCfmSerFree },
    { CSR_WIFI_ROUTER_CTRL_WIFI_ON_IND, CsrWifiRouterCtrlWifiOnIndSizeof, CsrWifiRouterCtrlWifiOnIndSer, CsrWifiRouterCtrlWifiOnIndDes, CsrWifiRouterCtrlWifiOnIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_WIFI_ON_CFM, CsrWifiRouterCtrlWifiOnCfmSizeof, CsrWifiRouterCtrlWifiOnCfmSer, CsrWifiRouterCtrlWifiOnCfmDes, CsrWifiRouterCtrlWifiOnCfmSerFree },
    { CSR_WIFI_ROUTER_CTRL_M4_READY_TO_SEND_IND, CsrWifiRouterCtrlM4ReadyToSendIndSizeof, CsrWifiRouterCtrlM4ReadyToSendIndSer, CsrWifiRouterCtrlM4ReadyToSendIndDes, CsrWifiRouterCtrlM4ReadyToSendIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_M4_TRANSMITTED_IND, CsrWifiRouterCtrlM4TransmittedIndSizeof, CsrWifiRouterCtrlM4TransmittedIndSer, CsrWifiRouterCtrlM4TransmittedIndDes, CsrWifiRouterCtrlM4TransmittedIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_MIC_FAILURE_IND, CsrWifiRouterCtrlMicFailureIndSizeof, CsrWifiRouterCtrlMicFailureIndSer, CsrWifiRouterCtrlMicFailureIndDes, CsrWifiRouterCtrlMicFailureIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_CONNECTED_IND, CsrWifiRouterCtrlConnectedIndSizeof, CsrWifiRouterCtrlConnectedIndSer, CsrWifiRouterCtrlConnectedIndDes, CsrWifiRouterCtrlConnectedIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_PEER_ADD_CFM, CsrWifiRouterCtrlPeerAddCfmSizeof, CsrWifiRouterCtrlPeerAddCfmSer, CsrWifiRouterCtrlPeerAddCfmDes, CsrWifiRouterCtrlPeerAddCfmSerFree },
    { CSR_WIFI_ROUTER_CTRL_PEER_DEL_CFM, CsrWifiRouterCtrlPeerDelCfmSizeof, CsrWifiRouterCtrlPeerDelCfmSer, CsrWifiRouterCtrlPeerDelCfmDes, CsrWifiRouterCtrlPeerDelCfmSerFree },
    { CSR_WIFI_ROUTER_CTRL_UNEXPECTED_FRAME_IND, CsrWifiRouterCtrlUnexpectedFrameIndSizeof, CsrWifiRouterCtrlUnexpectedFrameIndSer, CsrWifiRouterCtrlUnexpectedFrameIndDes, CsrWifiRouterCtrlUnexpectedFrameIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_PEER_UPDATE_CFM, CsrWifiRouterCtrlPeerUpdateCfmSizeof, CsrWifiRouterCtrlPeerUpdateCfmSer, CsrWifiRouterCtrlPeerUpdateCfmDes, CsrWifiRouterCtrlPeerUpdateCfmSerFree },
    { CSR_WIFI_ROUTER_CTRL_CAPABILITIES_CFM, CsrWifiRouterCtrlCapabilitiesCfmSizeof, CsrWifiRouterCtrlCapabilitiesCfmSer, CsrWifiRouterCtrlCapabilitiesCfmDes, CsrWifiRouterCtrlCapabilitiesCfmSerFree },
    { CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_ENABLE_CFM, CsrWifiRouterCtrlBlockAckEnableCfmSizeof, CsrWifiRouterCtrlBlockAckEnableCfmSer, CsrWifiRouterCtrlBlockAckEnableCfmDes, CsrWifiRouterCtrlBlockAckEnableCfmSerFree },
    { CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_DISABLE_CFM, CsrWifiRouterCtrlBlockAckDisableCfmSizeof, CsrWifiRouterCtrlBlockAckDisableCfmSer, CsrWifiRouterCtrlBlockAckDisableCfmDes, CsrWifiRouterCtrlBlockAckDisableCfmSerFree },
    { CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_ERROR_IND, CsrWifiRouterCtrlBlockAckErrorIndSizeof, CsrWifiRouterCtrlBlockAckErrorIndSer, CsrWifiRouterCtrlBlockAckErrorIndDes, CsrWifiRouterCtrlBlockAckErrorIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_STA_INACTIVE_IND, CsrWifiRouterCtrlStaInactiveIndSizeof, CsrWifiRouterCtrlStaInactiveIndSer, CsrWifiRouterCtrlStaInactiveIndDes, CsrWifiRouterCtrlStaInactiveIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_WAPI_RX_MIC_CHECK_IND, CsrWifiRouterCtrlWapiRxMicCheckIndSizeof, CsrWifiRouterCtrlWapiRxMicCheckIndSer, CsrWifiRouterCtrlWapiRxMicCheckIndDes, CsrWifiRouterCtrlWapiRxMicCheckIndSerFree },
    { CSR_WIFI_ROUTER_CTRL_MODE_SET_CFM, CsrWifiRouterCtrlModeSetCfmSizeof, CsrWifiRouterCtrlModeSetCfmSer, CsrWifiRouterCtrlModeSetCfmDes, CsrWifiRouterCtrlModeSetCfmSerFree },
    { CSR_WIFI_ROUTER_CTRL_WAPI_UNICAST_TX_ENCRYPT_IND, CsrWifiRouterCtrlWapiUnicastTxEncryptIndSizeof, CsrWifiRouterCtrlWapiUnicastTxEncryptIndSer, CsrWifiRouterCtrlWapiUnicastTxEncryptIndDes, CsrWifiRouterCtrlWapiUnicastTxEncryptIndSerFree },

    { 0, NULL, NULL, NULL, NULL },
};

CsrMsgConvMsgEntry* CsrWifiRouterCtrlConverterLookup(CsrMsgConvMsgEntry *ce, u16 msgType)
{
    if (msgType & CSR_PRIM_UPSTREAM)
    {
        u16 idx = (msgType & ~CSR_PRIM_UPSTREAM) + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_COUNT;
        if (idx < (CSR_WIFI_ROUTER_CTRL_PRIM_UPSTREAM_COUNT + CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_COUNT) &&
            csrwifirouterctrl_conv_lut[idx].msgType == msgType)
        {
            return &csrwifirouterctrl_conv_lut[idx];
        }
    }
    else
    {
        if (msgType < CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_COUNT &&
            csrwifirouterctrl_conv_lut[msgType].msgType == msgType)
        {
            return &csrwifirouterctrl_conv_lut[msgType];
        }
    }
    return NULL;
}


void CsrWifiRouterCtrlConverterInit(void)
{
    CsrMsgConvInsert(CSR_WIFI_ROUTER_CTRL_PRIM, csrwifirouterctrl_conv_lut);
    CsrMsgConvCustomLookupRegister(CSR_WIFI_ROUTER_CTRL_PRIM, CsrWifiRouterCtrlConverterLookup);
}


#ifdef CSR_LOG_ENABLE
static const CsrLogPrimitiveInformation csrwifirouterctrl_conv_info = {
    CSR_WIFI_ROUTER_CTRL_PRIM,
    (char *)"CSR_WIFI_ROUTER_CTRL_PRIM",
    csrwifirouterctrl_conv_lut
};
const CsrLogPrimitiveInformation* CsrWifiRouterCtrlTechInfoGet(void)
{
    return &csrwifirouterctrl_conv_info;
}


#endif /* CSR_LOG_ENABLE */
#endif /* EXCLUDE_CSR_WIFI_ROUTER_CTRL_MODULE */
