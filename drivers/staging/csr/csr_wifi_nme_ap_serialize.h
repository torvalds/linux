/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_NME_AP_SERIALIZE_H__
#define CSR_WIFI_NME_AP_SERIALIZE_H__

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

extern u8* CsrWifiNmeApConfigSetReqSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeApConfigSetReqDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeApConfigSetReqSizeof(void *msg);
extern void CsrWifiNmeApConfigSetReqSerFree(void *msg);

extern u8* CsrWifiNmeApWpsRegisterReqSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeApWpsRegisterReqDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeApWpsRegisterReqSizeof(void *msg);
#define CsrWifiNmeApWpsRegisterReqSerFree CsrWifiNmeApPfree

extern u8* CsrWifiNmeApStartReqSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeApStartReqDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeApStartReqSizeof(void *msg);
extern void CsrWifiNmeApStartReqSerFree(void *msg);

#define CsrWifiNmeApStopReqSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeApStopReqDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeApStopReqSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeApStopReqSerFree CsrWifiNmeApPfree

extern u8* CsrWifiNmeApWmmParamUpdateReqSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeApWmmParamUpdateReqDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeApWmmParamUpdateReqSizeof(void *msg);
#define CsrWifiNmeApWmmParamUpdateReqSerFree CsrWifiNmeApPfree

extern u8* CsrWifiNmeApStaRemoveReqSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeApStaRemoveReqDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeApStaRemoveReqSizeof(void *msg);
#define CsrWifiNmeApStaRemoveReqSerFree CsrWifiNmeApPfree

#define CsrWifiNmeApConfigSetCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeApConfigSetCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeApConfigSetCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeApConfigSetCfmSerFree CsrWifiNmeApPfree

extern u8* CsrWifiNmeApWpsRegisterCfmSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeApWpsRegisterCfmDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeApWpsRegisterCfmSizeof(void *msg);
#define CsrWifiNmeApWpsRegisterCfmSerFree CsrWifiNmeApPfree

extern u8* CsrWifiNmeApStartCfmSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeApStartCfmDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeApStartCfmSizeof(void *msg);
#define CsrWifiNmeApStartCfmSerFree CsrWifiNmeApPfree

extern u8* CsrWifiNmeApStopCfmSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeApStopCfmDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeApStopCfmSizeof(void *msg);
#define CsrWifiNmeApStopCfmSerFree CsrWifiNmeApPfree

extern u8* CsrWifiNmeApStopIndSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeApStopIndDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeApStopIndSizeof(void *msg);
#define CsrWifiNmeApStopIndSerFree CsrWifiNmeApPfree

#define CsrWifiNmeApWmmParamUpdateCfmSer CsrWifiEventCsrUint16Ser
#define CsrWifiNmeApWmmParamUpdateCfmDes CsrWifiEventCsrUint16Des
#define CsrWifiNmeApWmmParamUpdateCfmSizeof CsrWifiEventCsrUint16Sizeof
#define CsrWifiNmeApWmmParamUpdateCfmSerFree CsrWifiNmeApPfree

extern u8* CsrWifiNmeApStationIndSer(u8 *ptr, size_t *len, void *msg);
extern void* CsrWifiNmeApStationIndDes(u8 *buffer, size_t len);
extern size_t CsrWifiNmeApStationIndSizeof(void *msg);
#define CsrWifiNmeApStationIndSerFree CsrWifiNmeApPfree


#ifdef __cplusplus
}
#endif
#endif /* CSR_WIFI_NME_AP_SERIALIZE_H__ */

