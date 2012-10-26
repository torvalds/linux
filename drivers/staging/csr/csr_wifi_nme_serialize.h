/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_NME_SERIALIZE_H__
#define CSR_WIFI_NME_SERIALIZE_H__

#include "csr_wifi_msgconv.h"
#include "csr_wifi_nme_prim.h"

#ifndef CSR_WIFI_NME_ENABLE
#error CSR_WIFI_NME_ENABLE MUST be defined inorder to use csr_wifi_nme_serialize.h
#endif

extern void CsrWifiNmePfree(void *ptr);

extern u8* CsrWifiNmeProfileSetReqSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeProfileSetReqDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeProfileSetReqSizeof(void *msg);
extern void CsrWifiNmeProfileSetReqSerFree(void *msg);

extern u8* CsrWifiNmeProfileDeleteReqSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeProfileDeleteReqDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeProfileDeleteReqSizeof(void *msg);
#define CsrWifiNmeProfileDeleteReqSerFree CsrWifiNmePfree

#define CsrWifiNmeProfileDeleteAllReqSer CsrWifiEventSer
#define CsrWifiNmeProfileDeleteAllReqDes CsrWifiEventDes
#define CsrWifiNmeProfileDeleteAllReqSizeof CsrWifiEventSizeof
#define CsrWifiNmeProfileDeleteAllReqSerFree CsrWifiNmePfree

extern u8* CsrWifiNmeProfileOrderSetReqSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeProfileOrderSetReqDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeProfileOrderSetReqSizeof(void *msg);
extern void CsrWifiNmeProfileOrderSetReqSerFree(void *msg);

extern u8* CsrWifiNmeProfileConnectReqSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeProfileConnectReqDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeProfileConnectReqSizeof(void *msg);
#define CsrWifiNmeProfileConnectReqSerFree CsrWifiNmePfree

extern u8* CsrWifiNmeWpsReqSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeWpsReqDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeWpsReqSizeof(void *msg);
#define CsrWifiNmeWpsReqSerFree CsrWifiNmePfree

#define CsrWifiNmeWpsCancelReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeWpsCancelReqDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeWpsCancelReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeWpsCancelReqSerFree CsrWifiNmePfree

#define CsrWifiNmeConnectionStatusGetReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeConnectionStatusGetReqDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeConnectionStatusGetReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeConnectionStatusGetReqSerFree CsrWifiNmePfree

extern u8* CsrWifiNmeSimImsiGetResSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeSimImsiGetResDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeSimImsiGetResSizeof(void *msg);
extern void CsrWifiNmeSimImsiGetResSerFree(void *msg);

extern u8* CsrWifiNmeSimGsmAuthResSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeSimGsmAuthResDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeSimGsmAuthResSizeof(void *msg);
extern void CsrWifiNmeSimGsmAuthResSerFree(void *msg);

extern u8* CsrWifiNmeSimUmtsAuthResSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeSimUmtsAuthResDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeSimUmtsAuthResSizeof(void *msg);
extern void CsrWifiNmeSimUmtsAuthResSerFree(void *msg);

extern u8* CsrWifiNmeWpsConfigSetReqSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeWpsConfigSetReqDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeWpsConfigSetReqSizeof(void *msg);
extern void CsrWifiNmeWpsConfigSetReqSerFree(void *msg);

#define CsrWifiNmeEventMaskSetReqSer CsrWifiEventCsrUint32Ser
#define CsrWifiNmeEventMaskSetReqDes CsrWifiEventCsrUint32Des
#define CsrWifiNmeEventMaskSetReqSizeof CsrWifiEventCsrUint32Sizeof
#define CsrWifiNmeEventMaskSetReqSerFree CsrWifiNmePfree

#define CsrWifiNmeProfileSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeProfileSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeProfileSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeProfileSetCfmSerFree CsrWifiNmePfree

#define CsrWifiNmeProfileDeleteCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeProfileDeleteCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeProfileDeleteCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeProfileDeleteCfmSerFree CsrWifiNmePfree

#define CsrWifiNmeProfileDeleteAllCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeProfileDeleteAllCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeProfileDeleteAllCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeProfileDeleteAllCfmSerFree CsrWifiNmePfree

extern u8* CsrWifiNmeProfileOrderSetCfmSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeProfileOrderSetCfmDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeProfileOrderSetCfmSizeof(void *msg);
#define CsrWifiNmeProfileOrderSetCfmSerFree CsrWifiNmePfree

extern u8* CsrWifiNmeProfileConnectCfmSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeProfileConnectCfmDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeProfileConnectCfmSizeof(void *msg);
extern void CsrWifiNmeProfileConnectCfmSerFree(void *msg);

extern u8* CsrWifiNmeWpsCfmSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeWpsCfmDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeWpsCfmSizeof(void *msg);
extern void CsrWifiNmeWpsCfmSerFree(void *msg);

extern u8* CsrWifiNmeWpsCancelCfmSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeWpsCancelCfmDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeWpsCancelCfmSizeof(void *msg);
#define CsrWifiNmeWpsCancelCfmSerFree CsrWifiNmePfree

extern u8* CsrWifiNmeConnectionStatusGetCfmSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeConnectionStatusGetCfmDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeConnectionStatusGetCfmSizeof(void *msg);
#define CsrWifiNmeConnectionStatusGetCfmSerFree CsrWifiNmePfree

extern u8* CsrWifiNmeProfileUpdateIndSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeProfileUpdateIndDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeProfileUpdateIndSizeof(void *msg);
extern void CsrWifiNmeProfileUpdateIndSerFree(void *msg);

extern u8* CsrWifiNmeProfileDisconnectIndSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeProfileDisconnectIndDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeProfileDisconnectIndSizeof(void *msg);
extern void CsrWifiNmeProfileDisconnectIndSerFree(void *msg);

#define CsrWifiNmeSimImsiGetIndSer CsrWifiEventSer
#define CsrWifiNmeSimImsiGetIndDes CsrWifiEventDes
#define CsrWifiNmeSimImsiGetIndSizeof CsrWifiEventSizeof
#define CsrWifiNmeSimImsiGetIndSerFree CsrWifiNmePfree

extern u8* CsrWifiNmeSimGsmAuthIndSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeSimGsmAuthIndDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeSimGsmAuthIndSizeof(void *msg);
extern void CsrWifiNmeSimGsmAuthIndSerFree(void *msg);

extern u8* CsrWifiNmeSimUmtsAuthIndSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeSimUmtsAuthIndDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeSimUmtsAuthIndSizeof(void *msg);
#define CsrWifiNmeSimUmtsAuthIndSerFree CsrWifiNmePfree

#define CsrWifiNmeWpsConfigSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeWpsConfigSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeWpsConfigSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeWpsConfigSetCfmSerFree CsrWifiNmePfree

#define CsrWifiNmeEventMaskSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeEventMaskSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeEventMaskSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeEventMaskSetCfmSerFree CsrWifiNmePfree

#endif /* CSR_WIFI_NME_SERIALIZE_H__ */

