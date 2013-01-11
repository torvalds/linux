/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include "csr_sched.h"
#include "csr_msgconv.h"
#include "csr_macro.h"

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
    size_t length,
    u8 *data)
{
    CsrMsgConvPrimEntry *ptr;
    u8 *ret;

    ptr = CsrMsgConvFind(primType);

    if (ptr)
    {
        const CsrMsgConvMsgEntry *cv;
        u16 msgId = 0;
        size_t offset = 0;
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

static size_t sizeof_message(u16 primType, void *msg)
{
    CsrMsgConvPrimEntry *ptr = CsrMsgConvFind(primType);
    size_t ret;

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

static u8 free_message(u16 primType, u8 *data)
{
    CsrMsgConvPrimEntry *ptr;
    u8 ret;

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
    size_t *length,
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

size_t CsrMsgConvSizeof(u16 primType, void *msg)
{
    return sizeof_message(primType, msg);
}

u8 *CsrMsgConvSerialize(u8 *buffer, size_t maxBufferOffset, size_t *offset, u16 primType, void *msg)
{
    if (converter)
    {
        size_t serializedLength;
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
        pc = kmalloc(sizeof(*pc), GFP_KERNEL);
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
        converter = kmalloc(sizeof(CsrMsgConvEntry), GFP_KERNEL);

        converter->profile_converters = NULL;
        converter->free_message = free_message;
        converter->sizeof_message = sizeof_message;
        converter->serialize_message = serialize_message;
        converter->deserialize_data = deserialize_data;
    }

    return converter;
}
EXPORT_SYMBOL_GPL(CsrMsgConvInit);
