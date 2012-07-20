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

extern u8* CsrWifiRouterMaPacketSubscribeReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketSubscribeReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketSubscribeReqSizeof(void *msg);
#define CsrWifiRouterMaPacketSubscribeReqSerFree CsrWifiRouterPfree

#define CsrWifiRouterMaPacketUnsubscribeReqSer CsrWifiEventCsrUint16CsrUint8Ser
#define CsrWifiRouterMaPacketUnsubscribeReqDes CsrWifiEventCsrUint16CsrUint8Des
#define CsrWifiRouterMaPacketUnsubscribeReqSizeof CsrWifiEventCsrUint16CsrUint8Sizeof
#define CsrWifiRouterMaPacketUnsubscribeReqSerFree CsrWifiRouterPfree

extern u8* CsrWifiRouterMaPacketReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketReqSizeof(void *msg);
extern void CsrWifiRouterMaPacketReqSerFree(void *msg);

extern u8* CsrWifiRouterMaPacketResSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketResDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketResSizeof(void *msg);
#define CsrWifiRouterMaPacketResSerFree CsrWifiRouterPfree

extern u8* CsrWifiRouterMaPacketCancelReqSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketCancelReqDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketCancelReqSizeof(void *msg);
#define CsrWifiRouterMaPacketCancelReqSerFree CsrWifiRouterPfree

extern u8* CsrWifiRouterMaPacketSubscribeCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketSubscribeCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketSubscribeCfmSizeof(void *msg);
#define CsrWifiRouterMaPacketSubscribeCfmSerFree CsrWifiRouterPfree

extern u8* CsrWifiRouterMaPacketUnsubscribeCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketUnsubscribeCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketUnsubscribeCfmSizeof(void *msg);
#define CsrWifiRouterMaPacketUnsubscribeCfmSerFree CsrWifiRouterPfree

extern u8* CsrWifiRouterMaPacketCfmSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketCfmDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketCfmSizeof(void *msg);
#define CsrWifiRouterMaPacketCfmSerFree CsrWifiRouterPfree

extern u8* CsrWifiRouterMaPacketIndSer(u8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiRouterMaPacketIndDes(u8 *buffer, CsrSize len);
extern CsrSize CsrWifiRouterMaPacketIndSizeof(void *msg);
extern void CsrWifiRouterMaPacketIndSerFree(void *msg);


#ifdef __cplusplus
}
#endif
#endif /* CSR_WIFI_ROUTER_SERIALIZE_H__ */

