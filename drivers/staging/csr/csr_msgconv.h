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
typedef CsrUint8 *(CsrMsgSerializeFunc)(CsrUint8 *buffer, CsrSize *length, void *msg);
typedef void (CsrMsgFreeFunc)(void *msg);
typedef void *(CsrMsgDeserializeFunc)(CsrUint8 *buffer, CsrSize length);

/* Converter entry for one message type */
typedef struct CsrMsgConvMsgEntry
{
    CsrUint16              msgType;
    CsrMsgSizeofFunc      *sizeofFunc;
    CsrMsgSerializeFunc   *serFunc;
    CsrMsgDeserializeFunc *deserFunc;
    CsrMsgFreeFunc        *freeFunc;
} CsrMsgConvMsgEntry;

/* Optional lookup function */
typedef CsrMsgConvMsgEntry *(CsrMsgCustomLookupFunc)(CsrMsgConvMsgEntry *ce, CsrUint16 msgType);

/* All converter entries for one specific primitive */
typedef struct CsrMsgConvPrimEntry
{
    CsrUint16                   primType;
    const CsrMsgConvMsgEntry   *conv;
    CsrMsgCustomLookupFunc     *lookupFunc;
    struct CsrMsgConvPrimEntry *next;
} CsrMsgConvPrimEntry;

typedef struct
{
    CsrMsgConvPrimEntry *profile_converters;
    void *(*deserialize_data)(CsrUint16 primType, CsrSize length, CsrUint8 * data);
    CsrBool (*free_message)(CsrUint16 primType, CsrUint8 *data);
    CsrSize (*sizeof_message)(CsrUint16 primType, void *msg);
    CsrUint8 *(*serialize_message)(CsrUint16 primType, void *msg,
                                   CsrSize * length,
                                   CsrUint8 * buffer);
} CsrMsgConvEntry;

CsrSize CsrMsgConvSizeof(CsrUint16 primType, void *msg);
CsrUint8 *CsrMsgConvSerialize(CsrUint8 *buffer, CsrSize maxBufferOffset, CsrSize *offset, CsrUint16 primType, void *msg);
void CsrMsgConvCustomLookupRegister(CsrUint16 primType, CsrMsgCustomLookupFunc *lookupFunc);
void CsrMsgConvInsert(CsrUint16 primType, const CsrMsgConvMsgEntry *ce);
CsrMsgConvPrimEntry *CsrMsgConvFind(CsrUint16 primType);
CsrMsgConvMsgEntry *CsrMsgConvFindEntry(CsrUint16 primType, CsrUint16 msgType);
CsrMsgConvMsgEntry *CsrMsgConvFindEntryByMsg(CsrUint16 primType, const void *msg);
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
void CsrUint8Ser(CsrUint8 *buffer, CsrSize *offset, CsrUint8 value);
void CsrUint16Ser(CsrUint8 *buffer, CsrSize *offset, CsrUint16 value);
void CsrUint32Ser(CsrUint8 *buffer, CsrSize *offset, CsrUint32 value);
void CsrMemCpySer(CsrUint8 *buffer, CsrSize *offset, const void *value, CsrSize length);
void CsrCharStringSer(CsrUint8 *buffer, CsrSize *offset, const CsrCharString *value);
void CsrUtf8StringSer(CsrUint8 *buffer, CsrSize *offset, const CsrUtf8String *value);
void CsrUtf16StringSer(CsrUint8 *buffer, CsrSize *offset, const CsrUtf16String *value);
void CsrVoidPtrSer(CsrUint8 *buffer, CsrSize *offset, void *ptr);
void CsrSizeSer(CsrUint8 *buffer, CsrSize *offset, CsrSize value);

void CsrUint8Des(CsrUint8 *value, CsrUint8 *buffer, CsrSize *offset);
void CsrUint16Des(CsrUint16 *value, CsrUint8 *buffer, CsrSize *offset);
void CsrUint32Des(CsrUint32 *value, CsrUint8 *buffer, CsrSize *offset);
void CsrMemCpyDes(void *value, CsrUint8 *buffer, CsrSize *offset, CsrSize length);
void CsrCharStringDes(CsrCharString **value, CsrUint8 *buffer, CsrSize *offset);
void CsrUtf8StringDes(CsrUtf8String **value, CsrUint8 *buffer, CsrSize *offset);
void CsrUtf16StringDes(CsrUtf16String **value, CsrUint8 *buffer, CsrSize *offset);
void CsrVoidPtrDes(void **value, CsrUint8 *buffer, CsrSize *offset);
void CsrSizeDes(CsrSize *value, CsrUint8 *buffer, CsrSize *offset);

CsrSize CsrEventSizeof(void *msg);
CsrUint8 *CsrEventSer(CsrUint8 *ptr, CsrSize *len, void *msg);
void *CsrEventDes(CsrUint8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint8Sizeof(void *msg);
CsrUint8 *CsrEventCsrUint8Ser(CsrUint8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint8Des(CsrUint8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint16Sizeof(void *msg);
CsrUint8 *CsrEventCsrUint16Ser(CsrUint8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint16Des(CsrUint8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint32Sizeof(void *msg);
CsrUint8 *CsrEventCsrUint32Ser(CsrUint8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint32Des(CsrUint8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint16CsrUint8Sizeof(void *msg);
CsrUint8 *CsrEventCsrUint16CsrUint8Ser(CsrUint8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint16CsrUint8Des(CsrUint8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint16CsrUint16Sizeof(void *msg);
CsrUint8 *CsrEventCsrUint16CsrUint16Ser(CsrUint8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint16CsrUint16Des(CsrUint8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint16CsrUint32Sizeof(void *msg);
CsrUint8 *CsrEventCsrUint16CsrUint32Ser(CsrUint8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint16CsrUint32Des(CsrUint8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint16CsrCharStringSizeof(void *msg);
CsrUint8 *CsrEventCsrUint16CsrCharStringSer(CsrUint8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint16CsrCharStringDes(CsrUint8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint32CsrUint16Sizeof(void *msg);
CsrUint8 *CsrEventCsrUint32CsrUint16Ser(CsrUint8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint32CsrUint16Des(CsrUint8 *buffer, CsrSize length);

CsrSize CsrEventCsrUint32CsrCharStringSizeof(void *msg);
CsrUint8 *CsrEventCsrUint32CsrCharStringSer(CsrUint8 *ptr, CsrSize *len, void *msg);
void *CsrEventCsrUint32CsrCharStringDes(CsrUint8 *buffer, CsrSize length);

#ifdef __cplusplus
}
#endif

#endif
