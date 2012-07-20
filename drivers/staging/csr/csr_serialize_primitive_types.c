/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/module.h>
#include "csr_types.h"
#include "csr_prim_defs.h"
#include "csr_msgconv.h"
#include "csr_util.h"
#include "csr_pmem.h"
#include "csr_lib.h"

void CsrUint8Des(u8 *value, u8 *buffer, CsrSize *offset)
{
    *value = buffer[*offset];
    *offset += sizeof(*value);
}
EXPORT_SYMBOL_GPL(CsrUint8Des);

void CsrUint16Des(u16 *value, u8 *buffer, CsrSize *offset)
{
    *value = (buffer[*offset + 0] << 0) |
             (buffer[*offset + 1] << 8);
    *offset += sizeof(*value);
}
EXPORT_SYMBOL_GPL(CsrUint16Des);

void CsrUint32Des(u32 *value, u8 *buffer, CsrSize *offset)
{
    *value = (buffer[*offset + 0] << 0) |
             (buffer[*offset + 1] << 8) |
             (buffer[*offset + 2] << 16) |
             (buffer[*offset + 3] << 24);
    *offset += sizeof(*value);
}
EXPORT_SYMBOL_GPL(CsrUint32Des);

void CsrMemCpyDes(void *value, u8 *buffer, CsrSize *offset, CsrSize length)
{
    CsrMemCpy(value, &buffer[*offset], length);
    *offset += length;
}
EXPORT_SYMBOL_GPL(CsrMemCpyDes);

void CsrCharStringDes(CsrCharString **value, u8 *buffer, CsrSize *offset)
{
    *value = CsrStrDup((CsrCharString *) &buffer[*offset]);
    *offset += CsrStrLen(*value) + 1;
}
EXPORT_SYMBOL_GPL(CsrCharStringDes);

void CsrUtf8StringDes(CsrUtf8String **value, u8 *buffer, CsrSize *offset)
{
    *value = (CsrUtf8String *) CsrStrDup((CsrCharString *) &buffer[*offset]);
    *offset += CsrStrLen((CsrCharString *) *value) + 1;
}

void CsrUtf16StringDes(CsrUtf16String **value, u8 *buffer, CsrSize *offset)
{
    u32 length, i;

    CsrUint32Des(&length, buffer, offset);

    *value = CsrPmemAlloc(length * sizeof(**value));
    for (i = 0; i < length; i++)
    {
        CsrUint16Des(&(*value)[i], buffer, offset);
    }
}

void CsrSizeDes(CsrSize *value, u8 *buffer, CsrSize *offset)
{
    *value = (buffer[*offset + 0] << 0) |
             (buffer[*offset + 1] << 8) |
             (buffer[*offset + 2] << 16) |
             (buffer[*offset + 3] << 24);
    *offset += sizeof(*value);
}

void CsrVoidPtrDes(void **value, u8 *buffer, CsrSize *offset)
{
    CsrSizeDes((CsrSize *) value, buffer, offset);
}

void CsrUint8Ser(u8 *buffer, CsrSize *offset, u8 value)
{
    buffer[*offset] = value;
    *offset += sizeof(value);
}
EXPORT_SYMBOL_GPL(CsrUint8Ser);

void CsrUint16Ser(u8 *buffer, CsrSize *offset, u16 value)
{
    buffer[*offset + 0] = (u8) ((value >> 0) & 0xFF);
    buffer[*offset + 1] = (u8) ((value >> 8) & 0xFF);
    *offset += sizeof(value);
}
EXPORT_SYMBOL_GPL(CsrUint16Ser);

void CsrUint32Ser(u8 *buffer, CsrSize *offset, u32 value)
{
    buffer[*offset + 0] = (u8) ((value >> 0) & 0xFF);
    buffer[*offset + 1] = (u8) ((value >> 8) & 0xFF);
    buffer[*offset + 2] = (u8) ((value >> 16) & 0xFF);
    buffer[*offset + 3] = (u8) ((value >> 24) & 0xFF);
    *offset += sizeof(value);
}
EXPORT_SYMBOL_GPL(CsrUint32Ser);

void CsrMemCpySer(u8 *buffer, CsrSize *offset, const void *value, CsrSize length)
{
    CsrMemCpy(&buffer[*offset], value, length);
    *offset += length;
}
EXPORT_SYMBOL_GPL(CsrMemCpySer);

void CsrCharStringSer(u8 *buffer, CsrSize *offset, const CsrCharString *value)
{
    if (value)
    {
        CsrStrCpy(((CsrCharString *) &buffer[*offset]), value);
        *offset += CsrStrLen(value) + 1;
    }
    else
    {
        CsrUint8Ser(buffer, offset, 0);
    }
}
EXPORT_SYMBOL_GPL(CsrCharStringSer);

void CsrUtf8StringSer(u8 *buffer, CsrSize *offset, const CsrUtf8String *value)
{
    CsrCharStringSer(buffer, offset, (CsrCharString *) value);
}

void CsrUtf16StringSer(u8 *buffer, CsrSize *offset, const CsrUtf16String *value)
{
    if (value)
    {
        u32 length = CsrUtf16StrLen(value) + 1;
        u32 i;

        CsrUint32Ser(buffer, offset, length);

        for (i = 0; i < length; i++)
        {
            CsrUint16Ser(buffer, offset, (u16) value[i]);
        }
    }
    else
    {
        CsrUint32Ser(buffer, offset, 0);
    }
}

void CsrSizeSer(u8 *buffer, CsrSize *offset, CsrSize value)
{
    buffer[*offset + 0] = (u8) ((value >> 0) & 0xFF);
    buffer[*offset + 1] = (u8) ((value >> 8) & 0xFF);
    buffer[*offset + 2] = (u8) ((value >> 16) & 0xFF);
    buffer[*offset + 3] = (u8) ((value >> 24) & 0xFF);
    *offset += sizeof(value);
}

void CsrVoidPtrSer(u8 *buffer, CsrSize *offset, void *ptr)
{
    CsrSizeSer(buffer, offset, (CsrSize) ptr);
}

u32 CsrCharStringSerLen(const CsrCharString *str)
{
    if (str)
    {
        return (u32) (CsrStrLen(str) + sizeof(*str));
    }
    else
    {
        return sizeof(*str);
    }
}

u32 CsrUtf8StringSerLen(const CsrUtf8String *str)
{
    if (str)
    {
        return (u32) (CsrStrLen((CsrCharString *) str) + sizeof(*str));
    }
    else
    {
        return sizeof(*str);
    }
}

u32 CsrUtf16StringSerLen(const CsrUtf16String *str)
{
    if (str)
    {
        /* We always write down the length of the string */
        return sizeof(u32) + (CsrUtf16StrLen(str) + 1) * sizeof(*str);
    }
    else
    {
        return sizeof(u32);
    }
}

CsrSize CsrEventSizeof(void *msg)
{
    return 2;
}

u8 *CsrEventSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrEvent *primitive = (CsrEvent *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    return ptr;
}

void *CsrEventDes(u8 *buffer, CsrSize length)
{
    CsrEvent *primitive = (CsrEvent *) CsrPmemAlloc(sizeof(CsrEvent));
    CsrSize offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);

    return primitive;
}

CsrSize CsrEventCsrUint8Sizeof(void *msg)
{
    return 3;
}

u8 *CsrEventCsrUint8Ser(u8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint8 *primitive = (CsrEventCsrUint8 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint8Ser(ptr, len, primitive->value);
    return ptr;
}

void *CsrEventCsrUint8Des(u8 *buffer, CsrSize length)
{
    CsrEventCsrUint8 *primitive = (CsrEventCsrUint8 *) CsrPmemAlloc(sizeof(CsrEventCsrUint8));

    CsrSize offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint8Des(&primitive->value, buffer, &offset);

    return primitive;
}

CsrSize CsrEventCsrUint16Sizeof(void *msg)
{
    return 4;
}

u8 *CsrEventCsrUint16Ser(u8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint16 *primitive = (CsrEventCsrUint16 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value);
    return ptr;
}

void *CsrEventCsrUint16Des(u8 *buffer, CsrSize length)
{
    CsrEventCsrUint16 *primitive = (CsrEventCsrUint16 *) CsrPmemAlloc(sizeof(CsrEventCsrUint16));

    CsrSize offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint16Des(&primitive->value, buffer, &offset);

    return primitive;
}

CsrSize CsrEventCsrUint32Sizeof(void *msg)
{
    return 6;
}

u8 *CsrEventCsrUint32Ser(u8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint32 *primitive = (CsrEventCsrUint32 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint32Ser(ptr, len, primitive->value);
    return ptr;
}

void *CsrEventCsrUint32Des(u8 *buffer, CsrSize length)
{
    CsrEventCsrUint32 *primitive = (CsrEventCsrUint32 *) CsrPmemAlloc(sizeof(CsrEventCsrUint32));

    CsrSize offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint32Des(&primitive->value, buffer, &offset);

    return primitive;
}

CsrSize CsrEventCsrUint16CsrUint8Sizeof(void *msg)
{
    return 5;
}

u8 *CsrEventCsrUint16CsrUint8Ser(u8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint16CsrUint8 *primitive = (CsrEventCsrUint16CsrUint8 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value1);
    CsrUint8Ser(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint16CsrUint8Des(u8 *buffer, CsrSize length)
{
    CsrEventCsrUint16CsrUint8 *primitive = (CsrEventCsrUint16CsrUint8 *) CsrPmemAlloc(sizeof(CsrEventCsrUint16CsrUint8));

    CsrSize offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint16Des(&primitive->value1, buffer, &offset);
    CsrUint8Des(&primitive->value2, buffer, &offset);

    return primitive;
}

CsrSize CsrEventCsrUint16CsrUint16Sizeof(void *msg)
{
    return 6;
}

u8 *CsrEventCsrUint16CsrUint16Ser(u8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint16CsrUint16 *primitive = (CsrEventCsrUint16CsrUint16 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value1);
    CsrUint16Ser(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint16CsrUint16Des(u8 *buffer, CsrSize length)
{
    CsrEventCsrUint16CsrUint16 *primitive = (CsrEventCsrUint16CsrUint16 *) CsrPmemAlloc(sizeof(CsrEventCsrUint16CsrUint16));

    CsrSize offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint16Des(&primitive->value1, buffer, &offset);
    CsrUint16Des(&primitive->value2, buffer, &offset);

    return primitive;
}

CsrSize CsrEventCsrUint16CsrUint32Sizeof(void *msg)
{
    return 8;
}

u8 *CsrEventCsrUint16CsrUint32Ser(u8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint16CsrUint32 *primitive = (CsrEventCsrUint16CsrUint32 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value1);
    CsrUint32Ser(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint16CsrUint32Des(u8 *buffer, CsrSize length)
{
    CsrEventCsrUint16CsrUint32 *primitive = (CsrEventCsrUint16CsrUint32 *) CsrPmemAlloc(sizeof(CsrEventCsrUint16CsrUint32));

    CsrSize offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint16Des(&primitive->value1, buffer, &offset);
    CsrUint32Des(&primitive->value2, buffer, &offset);

    return primitive;
}

CsrSize CsrEventCsrUint16CsrCharStringSizeof(void *msg)
{
    CsrEventCsrUint16CsrCharString *primitive = (CsrEventCsrUint16CsrCharString *) msg;
    return 4 + CsrStrLen(primitive->value2) + 1;
}

u8 *CsrEventCsrUint16CsrCharStringSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint16CsrCharString *primitive = (CsrEventCsrUint16CsrCharString *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value1);
    CsrCharStringSer(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint16CsrCharStringDes(u8 *buffer, CsrSize length)
{
    CsrEventCsrUint16CsrCharString *primitive = (CsrEventCsrUint16CsrCharString *) CsrPmemAlloc(sizeof(CsrEventCsrUint16CsrCharString));

    CsrSize offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint16Des(&primitive->value1, buffer, &offset);
    CsrCharStringDes(&primitive->value2, buffer, &offset);

    return primitive;
}

CsrSize CsrEventCsrUint32CsrUint16Sizeof(void *msg)
{
    return 8;
}

u8 *CsrEventCsrUint32CsrUint16Ser(u8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint32CsrUint16 *primitive = (CsrEventCsrUint32CsrUint16 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint32Ser(ptr, len, primitive->value1);
    CsrUint16Ser(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint32CsrUint16Des(u8 *buffer, CsrSize length)
{
    CsrEventCsrUint32CsrUint16 *primitive = (CsrEventCsrUint32CsrUint16 *) CsrPmemAlloc(sizeof(CsrEventCsrUint32CsrUint16));

    CsrSize offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint32Des(&primitive->value1, buffer, &offset);
    CsrUint16Des(&primitive->value2, buffer, &offset);

    return primitive;
}

CsrSize CsrEventCsrUint32CsrCharStringSizeof(void *msg)
{
    CsrEventCsrUint32CsrCharString *primitive = (CsrEventCsrUint32CsrCharString *) msg;
    return 6 + CsrStrLen(primitive->value2) + 1;
}

u8 *CsrEventCsrUint32CsrCharStringSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint32CsrCharString *primitive = (CsrEventCsrUint32CsrCharString *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint32Ser(ptr, len, primitive->value1);
    CsrCharStringSer(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint32CsrCharStringDes(u8 *buffer, CsrSize length)
{
    CsrEventCsrUint32CsrCharString *primitive = (CsrEventCsrUint32CsrCharString *) CsrPmemAlloc(sizeof(CsrEventCsrUint32CsrCharString));

    CsrSize offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint32Des(&primitive->value1, buffer, &offset);
    CsrCharStringDes(&primitive->value2, buffer, &offset);

    return primitive;
}
