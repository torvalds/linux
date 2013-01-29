/*****************************************************************************

  (c) Cambridge Silicon Radio Limited 2010
  Confidential information of CSR

  Refer to LICENSE.txt included with this source for details
  on the license terms.

 *****************************************************************************/
#ifndef CSR_WIFI_ROUTER_SEF_CSR_WIFI_ROUTER_CTRL_H__
#define CSR_WIFI_ROUTER_SEF_CSR_WIFI_ROUTER_CTRL_H__

#include "csr_wifi_router_ctrl_prim.h"

    typedef void (*CsrWifiRouterCtrlStateHandlerType)(void* drvpriv, CsrWifiFsmEvent* msg);

    extern const CsrWifiRouterCtrlStateHandlerType CsrWifiRouterCtrlDownstreamStateHandlers[CSR_WIFI_ROUTER_CTRL_PRIM_DOWNSTREAM_COUNT];

    extern void CsrWifiRouterCtrlConfigurePowerModeReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlHipReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlMediaStatusReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlMulticastAddressResHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlPortConfigureReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlQosControlReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlSuspendResHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlTclasAddReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlResumeResHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlRawSdioDeinitialiseReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlRawSdioInitialiseReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlTclasDelReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlTrafficClassificationReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlTrafficConfigReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlWifiOffReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlWifiOffResHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlWifiOnReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlWifiOnResHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlM4TransmitReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlModeSetReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlPeerAddReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlPeerDelReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlPeerUpdateReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlCapabilitiesReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlBlockAckEnableReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlBlockAckDisableReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlWapiMulticastFilterReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlWapiRxPktReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlWapiUnicastTxPktReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlWapiUnicastFilterReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);
    extern void CsrWifiRouterCtrlWapiFilterReqHandler(void* drvpriv, CsrWifiFsmEvent* msg);

#endif /* CSR_WIFI_ROUTER_SEF_CSR_WIFI_ROUTER_CTRL_H__ */
