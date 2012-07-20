#ifndef CSR_MSGCONV_H__
#define CSR_MSGCONV_H__

/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include "csr_types.h"
#include "csr_prim_defs.h"
#include "csr_sched.h"
#include "csr_unicode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef CsrSize (CsrMsgSizeofFunc)(void *msg);
typedef u8 *(CsrMsgSerializeFunc)(u8 *buffer, CsrSize *length, void *msg);
typedef void (CsrMsgFreeFunc)(void *msg);
typedef void *(CsrMsgDeserializeFunc)(u8 *buffer, CsrSize length);

/* Converter entry for one message type */
typedef struct CsrMsgConvMsgEntry
{
    u16              msgType;
    CsrMsgSizeofFunc      *sizeofFunc;
    CsrMsgSerializeFunc   *serFunc;
    CsrMsgDeserializeFunc *deserFunc;
    CsrMsgFreeFunc        *freeFunc;
} CsrMsgConvMsgEntry;

/* Optional lookup function */
typedef CsrMsgConvMsgEntry *(CsrMsgCustomLookupFunc)(CsrMsgConvMsgEntry *ce, u16 msgType);

/* All converter entries for one specific primitive */
typedef struct CsrMsgConvPrimEntry
{
    u16                   primType;
    const CsrMsgConvMsgEntry   *conv;
    CsrMsgCustomLookupFunc     *lookupFunc;
    struct CsrMsgConvPrimEntry *next;
} CsrMsgConvPrimEntry;

typedef struct
{
    CsrMsgConvPrimEntry *profile_converters;
    void *(*deserialize_data)(u16 primType, CsrSize length, u8 * data);
    CsrBool (*free_message)(u16 primType, u8 *data);
    CsrSize (*sizeof_message)(u16 primType, void *msg);
    u8 *(*serialize_message)(u16 primType, void *msg,
                                   CsrSize * length,
                                   u8 * buffer);
} CsrMsgConvEntry;

CsrSize CsrMsgConvSizeof(u16 primType, void *msg);
u8 *CsrMsgConvSerialize(u8 *buffer, CsrSize maxBufferOffset, CsrSize *offset, u16 primType, void *msg);
void CsrMsgConvCustomLookupRegister(u16 primType, CsrMsgCustomLookupFunc *lookupFunc);
void CsrMsgConvInsert(u16 primType, const CsrMsgConvMsgEntry *ce);
CsrMsgConvPrimEntry *CsrMsgConvFind(u16 primType);
CsrMsgConvMsgEntry *CsrMsgConvFindEntry(u16 primType, u16 msgType);
CsrMsgConvMsgEntry *CsrMsgConvFindEntryByMsg(u16 primType, const void *msg);
CsrMsgConvEntry *CsrMsgConvGet(void);
CsrMsgConvEntry *CsrMsgConvInit(void);
#ifdef ENABLE_SHUTDOWN
void CsrMsgConvDeinit(void);
#endif /* ENABLE_SHUTDOWN */

/* SHOULD BE INTERNAL TO FRAMEWORK AKA DEPRECATED */

CsrUint32 CsrCharStringSerLen(const CsrCharString *str);
CsrUint32 CsrUtf8StringSerLen(const CsrUtf8String *str);
CsrUint32 CsrUtf16StringSerLen(const CsrUtf16String *str);

/* Prototypes for primitive type serializers */
void CsrUint8Ser(u8 *buffer, CsrSize *offset, u8 value);
void CsrUint16Ser(u8 *buffer, CsrSize *offset, u16 value);
void CsrUint32Ser(u8 *buffer, CsrSize *offset, CsrUint32 value);
void CsrMemCpySer(u8 *buffer, CsrSize *offset, const void *value, CsrSize length);
void CsrCharStringSer(u8 *buffer, CsrSize *offset, const CsrCharString *value);
void CsrUtf8StringSer(u8 *buffer, CsrSize *offset, const CsrUtf8String *value);
void CsrUtf16StringSer(u8 *buffer, CsrSize *offset, const CsrUtf16String *value);
void CsrVoidPtrSer(u8 *buffer, CsrSize *offset, void *ptr);
void CsrSizeSer(u8 *buffer, CsrSize *offset, CsrSize value);

void CsrUint8Des(u8 *value, u8 *buffer, CsrSize *offset);
void CsrUint16Des(u16 *value, u8 *buffer, CsrSize *offset);
void CsrUint32Des(CsrUint32 *value, u8 *buffer, CsrSize *offset);
void CsrMemCpyDes(void *value, u8 *buffer, CsrSize *offset, CsrSize length);
void CsrCharStringDes(CsrCharString **value, u8 *buffer, CsrSize *offset);
void CsrUtf8StringDes(CsrUtf8String **value, u8 *buffer, CsrSize *offset);
void CsrUtf16StringDes(CsrUtf16String **value, u8 *buffer, CsrSize *offset);
void CsrVoidPtrDes(void **value, u8 *buffer, CsrSize *offset);
void CsrSizeDes(CsrSize *value, u8 *buffer, CsrSize *offset);

CsrSize CsrEventSizeof(void *msg);
u8 *CsrEventSer(u8 *ptr, CsrSize *len, void *msg);
void *CsrEventDes(u8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint8Sizeof(void *msg);
u8 *CsrEventCsrUint8Ser(u8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint8Des(u8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint16Sizeof(void *msg);
u8 *CsrEventCsrUint16Ser(u8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint16Des(u8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint32Sizeof(void *msg);
u8 *CsrEventCsrUint32Ser(u8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint32Des(u8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint16CsrUint8Sizeof(void *msg);
u8 *CsrEventCsrUint16CsrUint8Ser(u8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint16CsrUint8Des(u8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint16CsrUint16Sizeof(void *msg);
u8 *CsrEventCsrUint16CsrUint16Ser(u8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint16CsrUint16Des(u8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint16CsrUint32Sizeof(void *msg);
u8 *CsrEventCsrUint16CsrUint32Ser(u8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint16CsrUint32Des(u8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint16CsrCharStringSizeof(void *msg);
u8 *CsrEventCsrUint16CsrCharStringSer(u8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint16CsrCharStringDes(u8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint32CsrUint16Sizeof(void *msg);
u8 *CsrEventCsrUint32CsrUint16Ser(u8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint32CsrUint16Des(u8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint32CsrCharStringSizeof(void *msg);
u8 *CsrEventCsrUint32CsrCharStringSer(u8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint32CsrCharStringDes(u8 *buffer, CsrSize length);

#ifdef __cplusplus
}
#endif

#endif
