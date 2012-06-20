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

extern CsrUint8* CsrWifiRouterCtrlConfigurePowerModeReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlConfigurePowerModeReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlConfigurePowerModeReqSizeof(void *msg);
#define CsrWifiRouterCtrlConfigurePowerModeReqSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlHipReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlHipReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlHipReqSizeof(void *msg);
extern void CsrWifiRouterCtrlHipReqSerFree(void *msg);

extern CsrUint8* CsrWifiRouterCtrlMediaStatusReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlMediaStatusReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlMediaStatusReqSizeof(void *msg);
#define CsrWifiRouterCtrlMediaStatusReqSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlMulticastAddressResSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlMulticastAddressResDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlMulticastAddressResSizeof(void *msg);
extern void CsrWifiRouterCtrlMulticastAddressResSerFree(void *msg);

extern CsrUint8* CsrWifiRouterCtrlPortConfigureReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPortConfigureReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPortConfigureReqSizeof(void *msg);
#define CsrWifiRouterCtrlPortConfigureReqSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlQosControlReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlQosControlReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlQosControlReqSizeof(void *msg);
#define CsrWifiRouterCtrlQosControlReqSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlSuspendResSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlSuspendResDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlSuspendResSizeof(void *msg);
#define CsrWifiRouterCtrlSuspendResSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlTclasAddReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTclasAddReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlTclasAddReqSizeof(void *msg);
extern void CsrWifiRouterCtrlTclasAddReqSerFree(void *msg);

extern CsrUint8* CsrWifiRouterCtrlResumeResSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlResumeResDes(CsrUint8 *buffer, CsrSize len);
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

extern CsrUint8* CsrWifiRouterCtrlTclasDelReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTclasDelReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlTclasDelReqSizeof(void *msg);
extern void CsrWifiRouterCtrlTclasDelReqSerFree(void *msg);

extern CsrUint8* CsrWifiRouterCtrlTrafficClassificationReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTrafficClassificationReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlTrafficClassificationReqSizeof(void *msg);
#define CsrWifiRouterCtrlTrafficClassificationReqSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlTrafficConfigReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTrafficConfigReqDes(CsrUint8 *buffer, CsrSize len);
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

extern CsrUint8* CsrWifiRouterCtrlWifiOnReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWifiOnReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWifiOnReqSizeof(void *msg);
extern void CsrWifiRouterCtrlWifiOnReqSerFree(void *msg);

extern CsrUint8* CsrWifiRouterCtrlWifiOnResSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWifiOnResDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWifiOnResSizeof(void *msg);
extern void CsrWifiRouterCtrlWifiOnResSerFree(void *msg);

extern CsrUint8* CsrWifiRouterCtrlM4TransmitReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlM4TransmitReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlM4TransmitReqSizeof(void *msg);
#define CsrWifiRouterCtrlM4TransmitReqSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlModeSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlModeSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlModeSetReqSizeof(void *msg);
#define CsrWifiRouterCtrlModeSetReqSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlPeerAddReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPeerAddReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPeerAddReqSizeof(void *msg);
#define CsrWifiRouterCtrlPeerAddReqSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlPeerDelReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPeerDelReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPeerDelReqSizeof(void *msg);
#define CsrWifiRouterCtrlPeerDelReqSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlPeerUpdateReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPeerUpdateReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPeerUpdateReqSizeof(void *msg);
#define CsrWifiRouterCtrlPeerUpdateReqSerFree CsrWifiRouterCtrlPfree

#define CsrWifiRouterCtrlCapabilitiesReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiRouterCtrlCapabilitiesReqDes CsrWifiEventCsrUint16Des
#define CsrWifiRouterCtrlCapabilitiesReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiRouterCtrlCapabilitiesReqSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlBlockAckEnableReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlBlockAckEnableReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlBlockAckEnableReqSizeof(void *msg);
#define CsrWifiRouterCtrlBlockAckEnableReqSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlBlockAckDisableReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlBlockAckDisableReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlBlockAckDisableReqSizeof(void *msg);
#define CsrWifiRouterCtrlBlockAckDisableReqSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlWapiRxPktReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWapiRxPktReqDes(CsrUint8 *buffer, CsrSize len);
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

extern CsrUint8* CsrWifiRouterCtrlWapiUnicastTxPktReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWapiUnicastTxPktReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWapiUnicastTxPktReqSizeof(void *msg);
extern void CsrWifiRouterCtrlWapiUnicastTxPktReqSerFree(void *msg);

#define CsrWifiRouterCtrlWapiFilterReqSer CsrWifiEventCsrUint16CsrUint8Ser
#define CsrWifiRouterCtrlWapiFilterReqDes CsrWifiEventCsrUint16CsrUint8Des
#define CsrWifiRouterCtrlWapiFilterReqSizeof CsrWifiEventCsrUint16CsrUint8Sizeof
#define CsrWifiRouterCtrlWapiFilterReqSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlHipIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlHipIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlHipIndSizeof(void *msg);
extern void CsrWifiRouterCtrlHipIndSerFree(void *msg);

extern CsrUint8* CsrWifiRouterCtrlMulticastAddressIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlMulticastAddressIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlMulticastAddressIndSizeof(void *msg);
extern void CsrWifiRouterCtrlMulticastAddressIndSerFree(void *msg);

extern CsrUint8* CsrWifiRouterCtrlPortConfigureCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPortConfigureCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPortConfigureCfmSizeof(void *msg);
#define CsrWifiRouterCtrlPortConfigureCfmSerFree CsrWifiRouterCtrlPfree

#define CsrWifiRouterCtrlResumeIndSer CsrWifiEventCsrUint16CsrUint8Ser
#define CsrWifiRouterCtrlResumeIndDes CsrWifiEventCsrUint16CsrUint8Des
#define CsrWifiRouterCtrlResumeIndSizeof CsrWifiEventCsrUint16CsrUint8Sizeof
#define CsrWifiRouterCtrlResumeIndSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlSuspendIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlSuspendIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlSuspendIndSizeof(void *msg);
#define CsrWifiRouterCtrlSuspendIndSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlTclasAddCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTclasAddCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlTclasAddCfmSizeof(void *msg);
#define CsrWifiRouterCtrlTclasAddCfmSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlRawSdioDeinitialiseCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlRawSdioDeinitialiseCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlRawSdioDeinitialiseCfmSizeof(void *msg);
#define CsrWifiRouterCtrlRawSdioDeinitialiseCfmSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlRawSdioInitialiseCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlRawSdioInitialiseCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlRawSdioInitialiseCfmSizeof(void *msg);
#define CsrWifiRouterCtrlRawSdioInitialiseCfmSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlTclasDelCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTclasDelCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlTclasDelCfmSizeof(void *msg);
#define CsrWifiRouterCtrlTclasDelCfmSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlTrafficProtocolIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTrafficProtocolIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlTrafficProtocolIndSizeof(void *msg);
#define CsrWifiRouterCtrlTrafficProtocolIndSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlTrafficSampleIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlTrafficSampleIndDes(CsrUint8 *buffer, CsrSize len);
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

extern CsrUint8* CsrWifiRouterCtrlWifiOnIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWifiOnIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWifiOnIndSizeof(void *msg);
extern void CsrWifiRouterCtrlWifiOnIndSerFree(void *msg);

extern CsrUint8* CsrWifiRouterCtrlWifiOnCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWifiOnCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWifiOnCfmSizeof(void *msg);
#define CsrWifiRouterCtrlWifiOnCfmSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlM4ReadyToSendIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlM4ReadyToSendIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlM4ReadyToSendIndSizeof(void *msg);
#define CsrWifiRouterCtrlM4ReadyToSendIndSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlM4TransmittedIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlM4TransmittedIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlM4TransmittedIndSizeof(void *msg);
#define CsrWifiRouterCtrlM4TransmittedIndSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlMicFailureIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlMicFailureIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlMicFailureIndSizeof(void *msg);
#define CsrWifiRouterCtrlMicFailureIndSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlConnectedIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlConnectedIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlConnectedIndSizeof(void *msg);
#define CsrWifiRouterCtrlConnectedIndSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlPeerAddCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPeerAddCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPeerAddCfmSizeof(void *msg);
#define CsrWifiRouterCtrlPeerAddCfmSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlPeerDelCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPeerDelCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPeerDelCfmSizeof(void *msg);
#define CsrWifiRouterCtrlPeerDelCfmSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlUnexpectedFrameIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlUnexpectedFrameIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlUnexpectedFrameIndSizeof(void *msg);
#define CsrWifiRouterCtrlUnexpectedFrameIndSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlPeerUpdateCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlPeerUpdateCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlPeerUpdateCfmSizeof(void *msg);
#define CsrWifiRouterCtrlPeerUpdateCfmSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlCapabilitiesCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlCapabilitiesCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlCapabilitiesCfmSizeof(void *msg);
#define CsrWifiRouterCtrlCapabilitiesCfmSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlBlockAckEnableCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlBlockAckEnableCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlBlockAckEnableCfmSizeof(void *msg);
#define CsrWifiRouterCtrlBlockAckEnableCfmSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlBlockAckDisableCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlBlockAckDisableCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlBlockAckDisableCfmSizeof(void *msg);
#define CsrWifiRouterCtrlBlockAckDisableCfmSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlBlockAckErrorIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlBlockAckErrorIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlBlockAckErrorIndSizeof(void *msg);
#define CsrWifiRouterCtrlBlockAckErrorIndSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlStaInactiveIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlStaInactiveIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlStaInactiveIndSizeof(void *msg);
#define CsrWifiRouterCtrlStaInactiveIndSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlWapiRxMicCheckIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWapiRxMicCheckIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWapiRxMicCheckIndSizeof(void *msg);
extern void CsrWifiRouterCtrlWapiRxMicCheckIndSerFree(void *msg);

extern CsrUint8* CsrWifiRouterCtrlModeSetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlModeSetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlModeSetCfmSizeof(void *msg);
#define CsrWifiRouterCtrlModeSetCfmSerFree CsrWifiRouterCtrlPfree

extern CsrUint8* CsrWifiRouterCtrlWapiUnicastTxEncryptIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterCtrlWapiUnicastTxEncryptIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterCtrlWapiUnicastTxEncryptIndSizeof(void *msg);
extern void CsrWifiRouterCtrlWapiUnicastTxEncryptIndSerFree(void *msg);


#ifdef __cplusplus
}
#endif
#endif /* CSR_WIFI_ROUTER_CTRL_SERIALIZE_H__ */

