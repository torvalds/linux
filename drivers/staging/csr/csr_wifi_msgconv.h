/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#ifndef CSR_WIFI_MSGCONV_H__
#define CSR_WIFI_MSGCONV_H__

#include "csr_types.h"
#include "csr_prim_defs.h"
#include "csr_sched.h"
#include "csr_unicode.h"

#ifdef __cplusplus
extern "C" {
#endif


void CsrUint16SerBigEndian(CsrUint8 *ptr, CsrSize *len, CsrUint16 v);
void CsrUint24SerBigEndian(CsrUint8 *ptr, CsrSize *len, CsrUint32 v);
void CsrUint32SerBigEndian(CsrUint8 *ptr, CsrSize *len, CsrUint32 v);

void CsrUint16DesBigEndian(CsrUint16 *v, CsrUint8 *buffer, CsrSize *offset);
void CsrUint24DesBigEndian(CsrUint32 *v, CsrUint8 *buffer, CsrSize *offset);
void CsrUint32DesBigEndian(CsrUint32 *v, CsrUint8 *buffer, CsrSize *offset);

void CsrUint24Ser(CsrUint8 *ptr, CsrSize *len, CsrUint32 v);
void CsrUint24Des(CsrUint32 *v, CsrUint8 *buffer, CsrSize *offset);


CsrSize CsrWifiEventSizeof(void *msg);
CsrUint8* CsrWifiEventSer(CsrUint8 *ptr, CsrSize *len, void *msg);
void* CsrWifiEventDes(CsrUint8 *buffer, CsrSize length);

CsrSize CsrWifiEventCsrUint8Sizeof(void *msg);
CsrUint8* CsrWifiEventCsrUint8Ser(CsrUint8 *ptr, CsrSize *len, void *msg);
void* CsrWifiEventCsrUint8Des(CsrUint8 *buffer, CsrSize length);

CsrSize CsrWifiEventCsrUint16Sizeof(void *msg);
CsrUint8* CsrWifiEventCsrUint16Ser(CsrUint8 *ptr, CsrSize *len, void *msg);
void* CsrWifiEventCsrUint16Des(CsrUint8 *buffer, CsrSize length);

CsrSize CsrWifiEventCsrUint32Sizeof(void *msg);
CsrUint8* CsrWifiEventCsrUint32Ser(CsrUint8 *ptr, CsrSize *len, void *msg);
void* CsrWifiEventCsrUint32Des(CsrUint8 *buffer, CsrSize length);

CsrSize CsrWifiEventCsrUint16CsrUint8Sizeof(void *msg);
CsrUint8* CsrWifiEventCsrUint16CsrUint8Ser(CsrUint8 *ptr, CsrSize *len, void *msg);
void* CsrWifiEventCsrUint16CsrUint8Des(CsrUint8 *buffer, CsrSize length);

#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_MSGCONV_H__ */
