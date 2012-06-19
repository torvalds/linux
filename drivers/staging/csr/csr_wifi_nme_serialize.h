/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_NME_SERIALIZE_H__
#define CSR_WIFI_NME_SERIALIZE_H__

#include "csr_types.h"
#include "csr_pmem.h"
#include "csr_wifi_msgconv.h"

#include "csr_wifi_nme_prim.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CSR_WIFI_NME_ENABLE
#error CSR_WIFI_NME_ENABLE MUST be defined inorder to use csr_wifi_nme_serialize.h
#endif

extern void CsrWifiNmePfree(void *ptr);

extern CsrUint8* CsrWifiNmeProfileSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeProfileSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeProfileSetReqSizeof(void *msg);
extern void CsrWifiNmeProfileSetReqSerFree(void *msg);

extern CsrUint8* CsrWifiNmeProfileDeleteReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeProfileDeleteReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeProfileDeleteReqSizeof(void *msg);
#define CsrWifiNmeProfileDeleteReqSerFree CsrWifiNmePfree

#define CsrWifiNmeProfileDeleteAllReqSer CsrWifiEventSer
#define CsrWifiNmeProfileDeleteAllReqDes CsrWifiEventDes
#define CsrWifiNmeProfileDeleteAllReqSizeof CsrWifiEventSizeof
#define CsrWifiNmeProfileDeleteAllReqSerFree CsrWifiNmePfree

extern CsrUint8* CsrWifiNmeProfileOrderSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeProfileOrderSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeProfileOrderSetReqSizeof(void *msg);
extern void CsrWifiNmeProfileOrderSetReqSerFree(void *msg);

extern CsrUint8* CsrWifiNmeProfileConnectReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeProfileConnectReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeProfileConnectReqSizeof(void *msg);
#define CsrWifiNmeProfileConnectReqSerFree CsrWifiNmePfree

extern CsrUint8* CsrWifiNmeWpsReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeWpsReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeWpsReqSizeof(void *msg);
#define CsrWifiNmeWpsReqSerFree CsrWifiNmePfree

#define CsrWifiNmeWpsCancelReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeWpsCancelReqDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeWpsCancelReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeWpsCancelReqSerFree CsrWifiNmePfree

#define CsrWifiNmeConnectionStatusGetReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeConnectionStatusGetReqDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeConnectionStatusGetReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeConnectionStatusGetReqSerFree CsrWifiNmePfree

extern CsrUint8* CsrWifiNmeSimImsiGetResSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeSimImsiGetResDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeSimImsiGetResSizeof(void *msg);
extern void CsrWifiNmeSimImsiGetResSerFree(void *msg);

extern CsrUint8* CsrWifiNmeSimGsmAuthResSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeSimGsmAuthResDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeSimGsmAuthResSizeof(void *msg);
extern void CsrWifiNmeSimGsmAuthResSerFree(void *msg);

extern CsrUint8* CsrWifiNmeSimUmtsAuthResSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeSimUmtsAuthResDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeSimUmtsAuthResSizeof(void *msg);
extern void CsrWifiNmeSimUmtsAuthResSerFree(void *msg);

extern CsrUint8* CsrWifiNmeWpsConfigSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeWpsConfigSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeWpsConfigSetReqSizeof(void *msg);
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

extern CsrUint8* CsrWifiNmeProfileOrderSetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeProfileOrderSetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeProfileOrderSetCfmSizeof(void *msg);
#define CsrWifiNmeProfileOrderSetCfmSerFree CsrWifiNmePfree

extern CsrUint8* CsrWifiNmeProfileConnectCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeProfileConnectCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeProfileConnectCfmSizeof(void *msg);
extern void CsrWifiNmeProfileConnectCfmSerFree(void *msg);

extern CsrUint8* CsrWifiNmeWpsCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeWpsCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeWpsCfmSizeof(void *msg);
extern void CsrWifiNmeWpsCfmSerFree(void *msg);

extern CsrUint8* CsrWifiNmeWpsCancelCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeWpsCancelCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeWpsCancelCfmSizeof(void *msg);
#define CsrWifiNmeWpsCancelCfmSerFree CsrWifiNmePfree

extern CsrUint8* CsrWifiNmeConnectionStatusGetCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeConnectionStatusGetCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeConnectionStatusGetCfmSizeof(void *msg);
#define CsrWifiNmeConnectionStatusGetCfmSerFree CsrWifiNmePfree

extern CsrUint8* CsrWifiNmeProfileUpdateIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeProfileUpdateIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeProfileUpdateIndSizeof(void *msg);
extern void CsrWifiNmeProfileUpdateIndSerFree(void *msg);

extern CsrUint8* CsrWifiNmeProfileDisconnectIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeProfileDisconnectIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeProfileDisconnectIndSizeof(void *msg);
extern void CsrWifiNmeProfileDisconnectIndSerFree(void *msg);

#define CsrWifiNmeSimImsiGetIndSer CsrWifiEventSer
#define CsrWifiNmeSimImsiGetIndDes CsrWifiEventDes
#define CsrWifiNmeSimImsiGetIndSizeof CsrWifiEventSizeof
#define CsrWifiNmeSimImsiGetIndSerFree CsrWifiNmePfree

extern CsrUint8* CsrWifiNmeSimGsmAuthIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeSimGsmAuthIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeSimGsmAuthIndSizeof(void *msg);
extern void CsrWifiNmeSimGsmAuthIndSerFree(void *msg);

extern CsrUint8* CsrWifiNmeSimUmtsAuthIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeSimUmtsAuthIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeSimUmtsAuthIndSizeof(void *msg);
#define CsrWifiNmeSimUmtsAuthIndSerFree CsrWifiNmePfree

#define CsrWifiNmeWpsConfigSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeWpsConfigSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeWpsConfigSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeWpsConfigSetCfmSerFree CsrWifiNmePfree

#define CsrWifiNmeEventMaskSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeEventMaskSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeEventMaskSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeEventMaskSetCfmSerFree CsrWifiNmePfree


#ifdef __cplusplus
}
#endif
#endif /* CSR_WIFI_NME_SERIALIZE_H__ */

