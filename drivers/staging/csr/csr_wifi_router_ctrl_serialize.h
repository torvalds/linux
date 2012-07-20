/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_ROUTER_CTRL_SERIALIZE_H__
#define CSR_WIFI_ROUTER_CTRL_SERIALIZE_H__

#include "csr_types.h"
#include "csr_pmem.h"
#include "csr_wifi_msgconv.h"

#include "csr_wifi_router_ctrl_prim.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void CsrWifiRouterCtrlPfree(void *ptr);

extern u8* CsrWifiRouterCtrlConfigurePowerModeReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlConfigurePowerModeReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlConfigurePowerModeReqSizeof(void *msg);
#define CsrWifiRouterCtrlConfigurePowerModeReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlHipReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlHipReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlHipReqSizeof(void *msg);
extern void CsrWifiRouterCtrlHipReqSerFree(void *msg);

extern u8* CsrWifiRouterCtrlMediaStatusReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlMediaStatusReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlMediaStatusReqSizeof(void *msg);
#define CsrWifiRouterCtrlMediaStatusReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlMulticastAddressResSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlMulticastAddressResDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlMulticastAddressResSizeof(void *msg);
extern void CsrWifiRouterCtrlMulticastAddressResSerFree(void *msg);

extern u8* CsrWifiRouterCtrlPortConfigureReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPortConfigureReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPortConfigureReqSizeof(void *msg);
#define CsrWifiRouterCtrlPortConfigureReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlQosControlReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlQosControlReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlQosControlReqSizeof(void *msg);
#define CsrWifiRouterCtrlQosControlReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlSuspendResSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlSuspendResDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlSuspendResSizeof(void *msg);
#define CsrWifiRouterCtrlSuspendResSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlTclasAddReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTclasAddReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlTclasAddReqSizeof(void *msg);
extern void CsrWifiRouterCtrlTclasAddReqSerFree(void *msg);

extern u8* CsrWifiRouterCtrlResumeResSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlResumeResDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlResumeResSizeof(void *msg);
#define CsrWifiRouterCtrlResumeResSerFree CsrWifiRouterCtrlPfree

#define CsrWifiRouterCtrlRawSdioDeinitialiseReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiRouterCtrlRawSdioDeinitialiseReqDes CsrWifiEventCsrUint16Des
#define CsrWifiRouterCtrlRawSdioDeinitialiseReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiRouterCtrlRawSdioDeinitialiseReqSerFree CsrWifiRouterCtrlPfree

#define CsrWifiRouterCtrlRawSdioInitialiseReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiRouterCtrlRawSdioInitialiseReqDes CsrWifiEventCsrUint16Des
#define CsrWifiRouterCtrlRawSdioInitialiseReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiRouterCtrlRawSdioInitialiseReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlTclasDelReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTclasDelReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlTclasDelReqSizeof(void *msg);
extern void CsrWifiRouterCtrlTclasDelReqSerFree(void *msg);

extern u8* CsrWifiRouterCtrlTrafficClassificationReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTrafficClassificationReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlTrafficClassificationReqSizeof(void *msg);
#define CsrWifiRouterCtrlTrafficClassificationReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlTrafficConfigReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTrafficConfigReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlTrafficConfigReqSizeof(void *msg);
#define CsrWifiRouterCtrlTrafficConfigReqSerFree CsrWifiRouterCtrlPfree

#define CsrWifiRouterCtrlWifiOffReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiRouterCtrlWifiOffReqDes CsrWifiEventCsrUint16Des
#define CsrWifiRouterCtrlWifiOffReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiRouterCtrlWifiOffReqSerFree CsrWifiRouterCtrlPfree

#define CsrWifiRouterCtrlWifiOffResSer CsrWifiEventCsrUint16Ser
#define CsrWifiRouterCtrlWifiOffResDes CsrWifiEventCsrUint16Des
#define CsrWifiRouterCtrlWifiOffResSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiRouterCtrlWifiOffResSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlWifiOnReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWifiOnReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWifiOnReqSizeof(void *msg);
extern void CsrWifiRouterCtrlWifiOnReqSerFree(void *msg);

extern u8* CsrWifiRouterCtrlWifiOnResSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWifiOnResDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWifiOnResSizeof(void *msg);
extern void CsrWifiRouterCtrlWifiOnResSerFree(void *msg);

extern u8* CsrWifiRouterCtrlM4TransmitReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlM4TransmitReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlM4TransmitReqSizeof(void *msg);
#define CsrWifiRouterCtrlM4TransmitReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlModeSetReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlModeSetReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlModeSetReqSizeof(void *msg);
#define CsrWifiRouterCtrlModeSetReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlPeerAddReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPeerAddReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPeerAddReqSizeof(void *msg);
#define CsrWifiRouterCtrlPeerAddReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlPeerDelReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPeerDelReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPeerDelReqSizeof(void *msg);
#define CsrWifiRouterCtrlPeerDelReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlPeerUpdateReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPeerUpdateReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPeerUpdateReqSizeof(void *msg);
#define CsrWifiRouterCtrlPeerUpdateReqSerFree CsrWifiRouterCtrlPfree

#define CsrWifiRouterCtrlCapabilitiesReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiRouterCtrlCapabilitiesReqDes CsrWifiEventCsrUint16Des
#define CsrWifiRouterCtrlCapabilitiesReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiRouterCtrlCapabilitiesReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlBlockAckEnableReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlBlockAckEnableReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlBlockAckEnableReqSizeof(void *msg);
#define CsrWifiRouterCtrlBlockAckEnableReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlBlockAckDisableReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlBlockAckDisableReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlBlockAckDisableReqSizeof(void *msg);
#define CsrWifiRouterCtrlBlockAckDisableReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlWapiRxPktReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWapiRxPktReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWapiRxPktReqSizeof(void *msg);
extern void CsrWifiRouterCtrlWapiRxPktReqSerFree(void *msg);

#define CsrWifiRouterCtrlWapiMulticastFilterReqSer CsrWifiEventCsrUint16CsrUint8Ser
#define CsrWifiRouterCtrlWapiMulticastFilterReqDes CsrWifiEventCsrUint16CsrUint8Des
#define CsrWifiRouterCtrlWapiMulticastFilterReqSizeof CsrWifiEventCsrUint16CsrUint8Sizeof
#define CsrWifiRouterCtrlWapiMulticastFilterReqSerFree CsrWifiRouterCtrlPfree

#define CsrWifiRouterCtrlWapiUnicastFilterReqSer CsrWifiEventCsrUint16CsrUint8Ser
#define CsrWifiRouterCtrlWapiUnicastFilterReqDes CsrWifiEventCsrUint16CsrUint8Des
#define CsrWifiRouterCtrlWapiUnicastFilterReqSizeof CsrWifiEventCsrUint16CsrUint8Sizeof
#define CsrWifiRouterCtrlWapiUnicastFilterReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlWapiUnicastTxPktReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWapiUnicastTxPktReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWapiUnicastTxPktReqSizeof(void *msg);
extern void CsrWifiRouterCtrlWapiUnicastTxPktReqSerFree(void *msg);

#define CsrWifiRouterCtrlWapiFilterReqSer CsrWifiEventCsrUint16CsrUint8Ser
#define CsrWifiRouterCtrlWapiFilterReqDes CsrWifiEventCsrUint16CsrUint8Des
#define CsrWifiRouterCtrlWapiFilterReqSizeof CsrWifiEventCsrUint16CsrUint8Sizeof
#define CsrWifiRouterCtrlWapiFilterReqSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlHipIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlHipIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlHipIndSizeof(void *msg);
extern void CsrWifiRouterCtrlHipIndSerFree(void *msg);

extern u8* CsrWifiRouterCtrlMulticastAddressIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlMulticastAddressIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlMulticastAddressIndSizeof(void *msg);
extern void CsrWifiRouterCtrlMulticastAddressIndSerFree(void *msg);

extern u8* CsrWifiRouterCtrlPortConfigureCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPortConfigureCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPortConfigureCfmSizeof(void *msg);
#define CsrWifiRouterCtrlPortConfigureCfmSerFree CsrWifiRouterCtrlPfree

#define CsrWifiRouterCtrlResumeIndSer CsrWifiEventCsrUint16CsrUint8Ser
#define CsrWifiRouterCtrlResumeIndDes CsrWifiEventCsrUint16CsrUint8Des
#define CsrWifiRouterCtrlResumeIndSizeof CsrWifiEventCsrUint16CsrUint8Sizeof
#define CsrWifiRouterCtrlResumeIndSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlSuspendIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlSuspendIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlSuspendIndSizeof(void *msg);
#define CsrWifiRouterCtrlSuspendIndSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlTclasAddCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTclasAddCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlTclasAddCfmSizeof(void *msg);
#define CsrWifiRouterCtrlTclasAddCfmSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlRawSdioDeinitialiseCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlRawSdioDeinitialiseCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlRawSdioDeinitialiseCfmSizeof(void *msg);
#define CsrWifiRouterCtrlRawSdioDeinitialiseCfmSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlRawSdioInitialiseCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlRawSdioInitialiseCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlRawSdioInitialiseCfmSizeof(void *msg);
#define CsrWifiRouterCtrlRawSdioInitialiseCfmSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlTclasDelCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTclasDelCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlTclasDelCfmSizeof(void *msg);
#define CsrWifiRouterCtrlTclasDelCfmSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlTrafficProtocolIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTrafficProtocolIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlTrafficProtocolIndSizeof(void *msg);
#define CsrWifiRouterCtrlTrafficProtocolIndSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlTrafficSampleIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTrafficSampleIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlTrafficSampleIndSizeof(void *msg);
#define CsrWifiRouterCtrlTrafficSampleIndSerFree CsrWifiRouterCtrlPfree

#define CsrWifiRouterCtrlWifiOffIndSer CsrWifiEventCsrUint16CsrUint8Ser
#define CsrWifiRouterCtrlWifiOffIndDes CsrWifiEventCsrUint16CsrUint8Des
#define CsrWifiRouterCtrlWifiOffIndSizeof CsrWifiEventCsrUint16CsrUint8Sizeof
#define CsrWifiRouterCtrlWifiOffIndSerFree CsrWifiRouterCtrlPfree

#define CsrWifiRouterCtrlWifiOffCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiRouterCtrlWifiOffCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiRouterCtrlWifiOffCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiRouterCtrlWifiOffCfmSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlWifiOnIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWifiOnIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWifiOnIndSizeof(void *msg);
extern void CsrWifiRouterCtrlWifiOnIndSerFree(void *msg);

extern u8* CsrWifiRouterCtrlWifiOnCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWifiOnCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWifiOnCfmSizeof(void *msg);
#define CsrWifiRouterCtrlWifiOnCfmSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlM4ReadyToSendIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlM4ReadyToSendIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlM4ReadyToSendIndSizeof(void *msg);
#define CsrWifiRouterCtrlM4ReadyToSendIndSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlM4TransmittedIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlM4TransmittedIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlM4TransmittedIndSizeof(void *msg);
#define CsrWifiRouterCtrlM4TransmittedIndSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlMicFailureIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlMicFailureIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlMicFailureIndSizeof(void *msg);
#define CsrWifiRouterCtrlMicFailureIndSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlConnectedIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlConnectedIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlConnectedIndSizeof(void *msg);
#define CsrWifiRouterCtrlConnectedIndSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlPeerAddCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPeerAddCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPeerAddCfmSizeof(void *msg);
#define CsrWifiRouterCtrlPeerAddCfmSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlPeerDelCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPeerDelCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPeerDelCfmSizeof(void *msg);
#define CsrWifiRouterCtrlPeerDelCfmSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlUnexpectedFrameIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlUnexpectedFrameIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlUnexpectedFrameIndSizeof(void *msg);
#define CsrWifiRouterCtrlUnexpectedFrameIndSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlPeerUpdateCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPeerUpdateCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPeerUpdateCfmSizeof(void *msg);
#define CsrWifiRouterCtrlPeerUpdateCfmSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlCapabilitiesCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlCapabilitiesCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlCapabilitiesCfmSizeof(void *msg);
#define CsrWifiRouterCtrlCapabilitiesCfmSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlBlockAckEnableCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlBlockAckEnableCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlBlockAckEnableCfmSizeof(void *msg);
#define CsrWifiRouterCtrlBlockAckEnableCfmSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlBlockAckDisableCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlBlockAckDisableCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlBlockAckDisableCfmSizeof(void *msg);
#define CsrWifiRouterCtrlBlockAckDisableCfmSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlBlockAckErrorIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlBlockAckErrorIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlBlockAckErrorIndSizeof(void *msg);
#define CsrWifiRouterCtrlBlockAckErrorIndSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlStaInactiveIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlStaInactiveIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlStaInactiveIndSizeof(void *msg);
#define CsrWifiRouterCtrlStaInactiveIndSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlWapiRxMicCheckIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWapiRxMicCheckIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWapiRxMicCheckIndSizeof(void *msg);
extern void CsrWifiRouterCtrlWapiRxMicCheckIndSerFree(void *msg);

extern u8* CsrWifiRouterCtrlModeSetCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlModeSetCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlModeSetCfmSizeof(void *msg);
#define CsrWifiRouterCtrlModeSetCfmSerFree CsrWifiRouterCtrlPfree

extern u8* CsrWifiRouterCtrlWapiUnicastTxEncryptIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWapiUnicastTxEncryptIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWapiUnicastTxEncryptIndSizeof(void *msg);
extern void CsrWifiRouterCtrlWapiUnicastTxEncryptIndSerFree(void *msg);


#ifdef __cplusplus
}
#endif
#endif /* CSR_WIFI_ROUTER_CTRL_SERIALIZE_H__ */

