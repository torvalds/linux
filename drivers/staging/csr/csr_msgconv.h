#ifndef CSR_MSGCONV_H__
#define CSR_MSGCONV_H__

/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/types.h>
#include "csr_prim_defs.h"
#include "csr_sched.h"

typedef size_t (CsrMsgSizeofFunc)(void *msg);
typedef u8 *(CsrMsgSerializeFunc)(u8 *buffer, size_t *length, void *msg);
typedef void (CsrMsgFreeFunc)(void *msg);
typedef void *(CsrMsgDeserializeFunc)(u8 *buffer, size_t length);

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
    void *(*deserialize_data)(u16 primType, size_t length, u8 * data);
    u8 (*free_message)(u16 primType, u8 *data);
    size_t (*sizeof_message)(u16 primType, void *msg);
    u8 *(*serialize_message)(u16 primType, void *msg,
                                   size_t * length,
                                   u8 * buffer);
} CsrMsgConvEntry;

size_t CsrMsgConvSizeof(u16 primType, void *msg);
u8 *CsrMsgConvSerialize(u8 *buffer, size_t maxBufferOffset, size_t *offset, u16 primType, void *msg);
void CsrMsgConvCustomLookupRegister(u16 primType, CsrMsgCustomLookupFunc *lookupFunc);
void CsrMsgConvInsert(u16 primType, const CsrMsgConvMsgEntry *ce);
CsrMsgConvPrimEntry *CsrMsgConvFind(u16 primType);
CsrMsgConvMsgEntry *CsrMsgConvFindEntry(u16 primType, u16 msgType);
CsrMsgConvMsgEntry *CsrMsgConvFindEntryByMsg(u16 primType, const void *msg);
CsrMsgConvEntry *CsrMsgConvInit(void);

/* Prototypes for primitive type serializers */
void CsrUint8Ser(u8 *buffer, size_t *offset, u8 value);
void CsrUint16Ser(u8 *buffer, size_t *offset, u16 value);
void CsrUint32Ser(u8 *buffer, size_t *offset, u32 value);
void CsrMemCpySer(u8 *buffer, size_t *offset, const void *value, size_t length);
void CsrCharStringSer(u8 *buffer, size_t *offset, const char *value);

void CsrUint8Des(u8 *value, u8 *buffer, size_t *offset);
void CsrUint16Des(u16 *value, u8 *buffer, size_t *offset);
void CsrUint32Des(u32 *value, u8 *buffer, size_t *offset);
void CsrMemCpyDes(void *value, u8 *buffer, size_t *offset, size_t length);
void CsrCharStringDes(char **value, u8 *buffer, size_t *offset);

#endif
