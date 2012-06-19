/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_ROUTER_SERIALIZE_H__
#define CSR_WIFI_ROUTER_SERIALIZE_H__

#include "csr_types.h"
#include "csr_pmem.h"
#include "csr_wifi_msgconv.h"

#include "csr_wifi_router_prim.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void CsrWifiRouterPfree(void *ptr);

extern CsrUint8* CsrWifiRouterMaPacketSubscribeReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketSubscribeReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketSubscribeReqSizeof(void *msg);
#define CsrWifiRouterMaPacketSubscribeReqSerFree CsrWifiRouterPfree

#define CsrWifiRouterMaPacketUnsubscribeReqSer CsrWifiEventCsrUint16CsrUint8Ser
#define CsrWifiRouterMaPacketUnsubscribeReqDes CsrWifiEventCsrUint16CsrUint8Des
#define CsrWifiRouterMaPacketUnsubscribeReqSizeof CsrWifiEventCsrUint16CsrUint8Sizeof
#define CsrWifiRouterMaPacketUnsubscribeReqSerFree CsrWifiRouterPfree

extern CsrUint8* CsrWifiRouterMaPacketReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketReqSizeof(void *msg);
extern void CsrWifiRouterMaPacketReqSerFree(void *msg);

extern CsrUint8* CsrWifiRouterMaPacketResSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketResDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketResSizeof(void *msg);
#define CsrWifiRouterMaPacketResSerFree CsrWifiRouterPfree

extern CsrUint8* CsrWifiRouterMaPacketCancelReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketCancelReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketCancelReqSizeof(void *msg);
#define CsrWifiRouterMaPacketCancelReqSerFree CsrWifiRouterPfree

extern CsrUint8* CsrWifiRouterMaPacketSubscribeCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketSubscribeCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketSubscribeCfmSizeof(void *msg);
#define CsrWifiRouterMaPacketSubscribeCfmSerFree CsrWifiRouterPfree

extern CsrUint8* CsrWifiRouterMaPacketUnsubscribeCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketUnsubscribeCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketUnsubscribeCfmSizeof(void *msg);
#define CsrWifiRouterMaPacketUnsubscribeCfmSerFree CsrWifiRouterPfree

extern CsrUint8* CsrWifiRouterMaPacketCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketCfmSizeof(void *msg);
#define CsrWifiRouterMaPacketCfmSerFree CsrWifiRouterPfree

extern CsrUint8* CsrWifiRouterMaPacketIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketIndSizeof(void *msg);
extern void CsrWifiRouterMaPacketIndSerFree(void *msg);


#ifdef __cplusplus
}
#endif
#endif /* CSR_WIFI_ROUTER_SERIALIZE_H__ */

