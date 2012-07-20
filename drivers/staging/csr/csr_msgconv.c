/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/module.h>
#include "csr_types.h"
#include "csr_pmem.h"
#include "csr_panic.h"
#include "csr_sched.h"
#include "csr_msgconv.h"
#include "csr_util.h"

static CsrMsgConvEntry *converter;

CsrMsgConvPrimEntry *CsrMsgConvFind(u16 primType)
{
    CsrMsgConvPrimEntry *ptr = NULL;

    if (converter)
    {
        ptr = converter->profile_converters;
        while (ptr)
        {
            if (ptr->primType == primType)
            {
                break;
            }
            else
            {
                ptr = ptr->next;
            }
        }
    }

    return ptr;
}

static const CsrMsgConvMsgEntry *find_msg_converter(CsrMsgConvPrimEntry *ptr, u16 msgType)
{
    const CsrMsgConvMsgEntry *cv = ptr->conv;
    if (ptr->lookupFunc)
    {
        return (const CsrMsgConvMsgEntry *) ptr->lookupFunc((CsrMsgConvMsgEntry *) cv, msgType);
    }

    while (cv)
    {
        if (cv->serFunc == NULL)
        {
            /* We've reached the end of the chain */
            cv = NULL;
            break;
        }

        if (cv->msgType == msgType)
        {
            break;
        }
        else
        {
            cv++;
        }
    }

    return cv;
}

static void *deserialize_data(u16 primType,
    CsrSize length,
    u8 *data)
{
    CsrMsgConvPrimEntry *ptr;
    u8 *ret;

    ptr = CsrMsgConvFind(primType);

    if (ptr)
    {
        const CsrMsgConvMsgEntry *cv;
        u16 msgId = 0;
        CsrSize offset = 0;
        CsrUint16Des(&msgId, data, &offset);

        cv = find_msg_converter(ptr, msgId);
        if (cv)
        {
            ret = cv->deserFunc(data, length);
        }
        else
        {
            ret = NULL;
        }
    }
    else
    {
        ret = NULL;
    }

    return ret;
}

static CsrSize sizeof_message(u16 primType, void *msg)
{
    CsrMsgConvPrimEntry *ptr = CsrMsgConvFind(primType);
    CsrSize ret;

    if (ptr)
    {
        const CsrMsgConvMsgEntry *cv;
        u16 msgId = *(u16 *) msg;

        cv = find_msg_converter(ptr, msgId);
        if (cv)
        {
            ret = cv->sizeofFunc(msg);
        }
        else
        {
            ret = 0;
        }
    }
    else
    {
        ret = 0;
    }

    return ret;
}

static CsrBool free_message(u16 primType, u8 *data)
{
    CsrMsgConvPrimEntry *ptr;
    CsrBool ret;

    ptr = CsrMsgConvFind(primType);

    if (ptr)
    {
        const CsrMsgConvMsgEntry *cv;
        u16 msgId = *(u16 *) data;

        cv = find_msg_converter(ptr, msgId);
        if (cv)
        {
            cv->freeFunc(data);
            ret = TRUE;
        }
        else
        {
            ret = FALSE;
        }
    }
    else
    {
        ret = FALSE;
    }

    return ret;
}

static u8 *serialize_message(u16 primType,
    void *msg,
    CsrSize *length,
    u8 *buffer)
{
    CsrMsgConvPrimEntry *ptr;
    u8 *ret;

    ptr = CsrMsgConvFind(primType);

    *length = 0;

    if (ptr)
    {
        const CsrMsgConvMsgEntry *cv;

        cv = find_msg_converter(ptr, *(u16 *) msg);
        if (cv)
        {
            ret = cv->serFunc(buffer, length, msg);
        }
        else
        {
            ret = NULL;
        }
    }
    else
    {
        ret = NULL;
    }

    return ret;
}

CsrSize CsrMsgConvSizeof(u16 primType, void *msg)
{
    return sizeof_message(primType, msg);
}

u8 *CsrMsgConvSerialize(u8 *buffer, CsrSize maxBufferOffset, CsrSize *offset, u16 primType, void *msg)
{
    if (converter)
    {
        CsrSize serializedLength;
        u8 *bufSerialized;
        u8 *bufOffset = &buffer[*offset];
        bufSerialized = converter->serialize_message(primType, msg, &serializedLength, bufOffset);
        *offset += serializedLength;
        return bufSerialized;
    }
    else
    {
        return NULL;
    }
}

/* Insert profile converter at head of converter list. */
void CsrMsgConvInsert(u16 primType, const CsrMsgConvMsgEntry *ce)
{
    CsrMsgConvPrimEntry *pc;
    pc = CsrMsgConvFind(primType);

    if (pc)
    {
        /* Already registered. Do nothing */
    }
    else
    {
        pc = CsrPmemAlloc(sizeof(*pc));
        pc->primType = primType;
        pc->conv = ce;
        pc->lookupFunc = NULL;
        pc->next = converter->profile_converters;
        converter->profile_converters = pc;
    }
}
EXPORT_SYMBOL_GPL(CsrMsgConvInsert);

CsrMsgConvMsgEntry *CsrMsgConvFindEntry(u16 primType, u16 msgType)
{
    CsrMsgConvPrimEntry *ptr = CsrMsgConvFind(primType);
    if (ptr)
    {
        return (CsrMsgConvMsgEntry *) find_msg_converter(ptr, msgType);
    }
    return NULL;
}
EXPORT_SYMBOL_GPL(CsrMsgConvFindEntry);

CsrMsgConvMsgEntry *CsrMsgConvFindEntryByMsg(u16 primType, const void *msg)
{
    CsrMsgConvPrimEntry *ptr = CsrMsgConvFind(primType);
    if (ptr && msg)
    {
        u16 msgType = *((u16 *) msg);
        return (CsrMsgConvMsgEntry *) find_msg_converter(ptr, msgType);
    }
    return NULL;
}

void CsrMsgConvCustomLookupRegister(u16 primType, CsrMsgCustomLookupFunc *lookupFunc)
{
    CsrMsgConvPrimEntry *ptr = CsrMsgConvFind(primType);
    if (ptr)
    {
        ptr->lookupFunc = lookupFunc;
    }
}
EXPORT_SYMBOL_GPL(CsrMsgConvCustomLookupRegister);

CsrMsgConvEntry *CsrMsgConvInit(void)
{
    if (!converter)
    {
        converter = (CsrMsgConvEntry *) CsrPmemAlloc(sizeof(CsrMsgConvEntry));

        converter->profile_converters = NULL;
        converter->free_message = free_message;
        converter->sizeof_message = sizeof_message;
        converter->serialize_message = serialize_message;
        converter->deserialize_data = deserialize_data;
    }

    return converter;
}
EXPORT_SYMBOL_GPL(CsrMsgConvInit);

CsrMsgConvEntry *CsrMsgConvGet(void)
{
    return converter;
}

#ifdef ENABLE_SHUTDOWN
void CsrMsgConvDeinit(void)
{
    CsrMsgConvPrimEntry *s;

    if (converter == NULL)
    {
        return;
    }

    /* Walk converter list and free elements. */
    s = converter->profile_converters;
    while (s)
    {
        CsrMsgConvPrimEntry *s_next;
        s_next = s->next;
        CsrPmemFree(s);
        s = s_next;
    }

    CsrPmemFree(converter);
    converter = NULL;
}
EXPORT_SYMBOL_GPL(CsrMsgConvDeinit);

#endif /* ENABLE_SHUTDOWN */
