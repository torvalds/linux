/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_NME_AP_SERIALIZE_H__
#define CSR_WIFI_NME_AP_SERIALIZE_H__

#include "csr_types.h"
#include "csr_pmem.h"
#include "csr_wifi_msgconv.h"

#include "csr_wifi_nme_ap_prim.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CSR_WIFI_NME_ENABLE
#error CSR_WIFI_NME_ENABLE MUST be defined inorder to use csr_wifi_nme_ap_serialize.h
#endif
#ifndef CSR_WIFI_AP_ENABLE
#error CSR_WIFI_AP_ENABLE MUST be defined inorder to use csr_wifi_nme_ap_serialize.h
#endif

extern void CsrWifiNmeApPfree(void *ptr);

extern CsrUint8* CsrWifiNmeApConfigSetReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeApConfigSetReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeApConfigSetReqSizeof(void *msg);
extern void CsrWifiNmeApConfigSetReqSerFree(void *msg);

extern CsrUint8* CsrWifiNmeApWpsRegisterReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeApWpsRegisterReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeApWpsRegisterReqSizeof(void *msg);
#define CsrWifiNmeApWpsRegisterReqSerFree CsrWifiNmeApPfree

extern CsrUint8* CsrWifiNmeApStartReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeApStartReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeApStartReqSizeof(void *msg);
extern void CsrWifiNmeApStartReqSerFree(void *msg);

#define CsrWifiNmeApStopReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeApStopReqDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeApStopReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeApStopReqSerFree CsrWifiNmeApPfree

extern CsrUint8* CsrWifiNmeApWmmParamUpdateReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeApWmmParamUpdateReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeApWmmParamUpdateReqSizeof(void *msg);
#define CsrWifiNmeApWmmParamUpdateReqSerFree CsrWifiNmeApPfree

extern CsrUint8* CsrWifiNmeApStaRemoveReqSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeApStaRemoveReqDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeApStaRemoveReqSizeof(void *msg);
#define CsrWifiNmeApStaRemoveReqSerFree CsrWifiNmeApPfree

#define CsrWifiNmeApConfigSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeApConfigSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeApConfigSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeApConfigSetCfmSerFree CsrWifiNmeApPfree

extern CsrUint8* CsrWifiNmeApWpsRegisterCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeApWpsRegisterCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeApWpsRegisterCfmSizeof(void *msg);
#define CsrWifiNmeApWpsRegisterCfmSerFree CsrWifiNmeApPfree

extern CsrUint8* CsrWifiNmeApStartCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeApStartCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeApStartCfmSizeof(void *msg);
#define CsrWifiNmeApStartCfmSerFree CsrWifiNmeApPfree

extern CsrUint8* CsrWifiNmeApStopCfmSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeApStopCfmDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeApStopCfmSizeof(void *msg);
#define CsrWifiNmeApStopCfmSerFree CsrWifiNmeApPfree

extern CsrUint8* CsrWifiNmeApStopIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeApStopIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeApStopIndSizeof(void *msg);
#define CsrWifiNmeApStopIndSerFree CsrWifiNmeApPfree

#define CsrWifiNmeApWmmParamUpdateCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeApWmmParamUpdateCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeApWmmParamUpdateCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeApWmmParamUpdateCfmSerFree CsrWifiNmeApPfree

extern CsrUint8* CsrWifiNmeApStationIndSer(CsrUint8 *ptr, CsrSize *len, void *msg);
extern void* CsrWifiNmeApStationIndDes(CsrUint8 *buffer, CsrSize len);
extern CsrSize CsrWifiNmeApStationIndSizeof(void *msg);
#define CsrWifiNmeApStationIndSerFree CsrWifiNmeApPfree


#ifdef __cplusplus
}
#endif
#endif /* CSR_WIFI_NME_AP_SERIALIZE_H__ */

