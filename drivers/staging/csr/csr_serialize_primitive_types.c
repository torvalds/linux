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

void CsrUint8Des(CsrUint8 *value, CsrUint8 *buffer, CsrSize *offset)
{
    *value = buffer[*offset];
    *offset += sizeof(*value);
}
EXPORT_SYMBOL_GPL(CsrUint8Des);

void CsrUint16Des(CsrUint16 *value, CsrUint8 *buffer, CsrSize *offset)
{
    *value = (buffer[*offset + 0] << 0) |
             (buffer[*offset + 1] << 8);
    *offset += sizeof(*value);
}
EXPORT_SYMBOL_GPL(CsrUint16Des);

void CsrUint32Des(CsrUint32 *value, CsrUint8 *buffer, CsrSize *offset)
{
    *value = (buffer[*offset + 0] << 0) |
             (buffer[*offset + 1] << 8) |
             (buffer[*offset + 2] << 16) |
             (buffer[*offset + 3] << 24);
    *offset += sizeof(*value);
}
EXPORT_SYMBOL_GPL(CsrUint32Des);

void CsrMemCpyDes(void *value, CsrUint8 *buffer, CsrSize *offset, CsrSize length)
{
    CsrMemCpy(value, &buffer[*offset], length);
    *offset += length;
}
EXPORT_SYMBOL_GPL(CsrMemCpyDes);

void CsrCharStringDes(CsrCharString **value, CsrUint8 *buffer, CsrSize *offset)
{
    *value = CsrStrDup((CsrCharString *) &buffer[*offset]);
    *offset += CsrStrLen(*value) + 1;
}
EXPORT_SYMBOL_GPL(CsrCharStringDes);

void CsrUtf8StringDes(CsrUtf8String **value, CsrUint8 *buffer, CsrSize *offset)
{
    *value = (CsrUtf8String *) CsrStrDup((CsrCharString *) &buffer[*offset]);
    *offset += CsrStrLen((CsrCharString *) *value) + 1;
}

void CsrUtf16StringDes(CsrUtf16String **value, CsrUint8 *buffer, CsrSize *offset)
{
    CsrUint32 length, i;

    CsrUint32Des(&length, buffer, offset);

    *value = CsrPmemAlloc(length * sizeof(**value));
    for (i = 0; i < length; i++)
    {
        CsrUint16Des(&(*value)[i], buffer, offset);
    }
}

void CsrSizeDes(CsrSize *value, CsrUint8 *buffer, CsrSize *offset)
{
    *value = (buffer[*offset + 0] << 0) |
             (buffer[*offset + 1] << 8) |
             (buffer[*offset + 2] << 16) |
             (buffer[*offset + 3] << 24);
    *offset += sizeof(*value);
}

void CsrVoidPtrDes(void **value, CsrUint8 *buffer, CsrSize *offset)
{
    CsrSizeDes((CsrSize *) value, buffer, offset);
}

void CsrUint8Ser(CsrUint8 *buffer, CsrSize *offset, CsrUint8 value)
{
    buffer[*offset] = value;
    *offset += sizeof(value);
}
EXPORT_SYMBOL_GPL(CsrUint8Ser);

void CsrUint16Ser(CsrUint8 *buffer, CsrSize *offset, CsrUint16 value)
{
    buffer[*offset + 0] = (CsrUint8) ((value >> 0) & 0xFF);
    buffer[*offset + 1] = (CsrUint8) ((value >> 8) & 0xFF);
    *offset += sizeof(value);
}
EXPORT_SYMBOL_GPL(CsrUint16Ser);

void CsrUint32Ser(CsrUint8 *buffer, CsrSize *offset, CsrUint32 value)
{
    buffer[*offset + 0] = (CsrUint8) ((value >> 0) & 0xFF);
    buffer[*offset + 1] = (CsrUint8) ((value >> 8) & 0xFF);
    buffer[*offset + 2] = (CsrUint8) ((value >> 16) & 0xFF);
    buffer[*offset + 3] = (CsrUint8) ((value >> 24) & 0xFF);
    *offset += sizeof(value);
}
EXPORT_SYMBOL_GPL(CsrUint32Ser);

void CsrMemCpySer(CsrUint8 *buffer, CsrSize *offset, const void *value, CsrSize length)
{
    CsrMemCpy(&buffer[*offset], value, length);
    *offset += length;
}
EXPORT_SYMBOL_GPL(CsrMemCpySer);

void CsrCharStringSer(CsrUint8 *buffer, CsrSize *offset, const CsrCharString *value)
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

void CsrUtf8StringSer(CsrUint8 *buffer, CsrSize *offset, const CsrUtf8String *value)
{
    CsrCharStringSer(buffer, offset, (CsrCharString *) value);
}

void CsrUtf16StringSer(CsrUint8 *buffer, CsrSize *offset, const CsrUtf16String *value)
{
    if (value)
    {
        CsrUint32 length = CsrUtf16StrLen(value) + 1;
        CsrUint32 i;

        CsrUint32Ser(buffer, offset, length);

        for (i = 0; i < length; i++)
        {
            CsrUint16Ser(buffer, offset, (CsrUint16) value[i]);
        }
    }
    else
    {
        CsrUint32Ser(buffer, offset, 0);
    }
}

void CsrSizeSer(CsrUint8 *buffer, CsrSize *offset, CsrSize value)
{
    buffer[*offset + 0] = (CsrUint8) ((value >> 0) & 0xFF);
    buffer[*offset + 1] = (CsrUint8) ((value >> 8) & 0xFF);
    buffer[*offset + 2] = (CsrUint8) ((value >> 16) & 0xFF);
    buffer[*offset + 3] = (CsrUint8) ((value >> 24) & 0xFF);
    *offset += sizeof(value);
}

void CsrVoidPtrSer(CsrUint8 *buffer, CsrSize *offset, void *ptr)
{
    CsrSizeSer(buffer, offset, (CsrSize) ptr);
}

CsrUint32 CsrCharStringSerLen(const CsrCharString *str)
{
    if (str)
    {
        return (CsrUint32) (CsrStrLen(str) + sizeof(*str));
    }
    else
    {
        return sizeof(*str);
    }
}

CsrUint32 CsrUtf8StringSerLen(const CsrUtf8String *str)
{
    if (str)
    {
        return (CsrUint32) (CsrStrLen((CsrCharString *) str) + sizeof(*str));
    }
    else
    {
        return sizeof(*str);
    }
}

CsrUint32 CsrUtf16StringSerLen(const CsrUtf16String *str)
{
    if (str)
    {
        /* We always write down the length of the string */
        return sizeof(CsrUint32) + (CsrUtf16StrLen(str) + 1) * sizeof(*str);
    }
    else
    {
        return sizeof(CsrUint32);
    }
}

CsrSize CsrEventSizeof(void *msg)
{
    return 2;
}

CsrUint8 *CsrEventSer(CsrUint8 *ptr, CsrSize *len, void *msg)
{
    CsrEvent *primitive = (CsrEvent *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    return ptr;
}

void *CsrEventDes(CsrUint8 *buffer, CsrSize length)
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

CsrUint8 *CsrEventCsrUint8Ser(CsrUint8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint8 *primitive = (CsrEventCsrUint8 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint8Ser(ptr, len, primitive->value);
    return ptr;
}

void *CsrEventCsrUint8Des(CsrUint8 *buffer, CsrSize length)
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

CsrUint8 *CsrEventCsrUint16Ser(CsrUint8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint16 *primitive = (CsrEventCsrUint16 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value);
    return ptr;
}

void *CsrEventCsrUint16Des(CsrUint8 *buffer, CsrSize length)
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

CsrUint8 *CsrEventCsrUint32Ser(CsrUint8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint32 *primitive = (CsrEventCsrUint32 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint32Ser(ptr, len, primitive->value);
    return ptr;
}

void *CsrEventCsrUint32Des(CsrUint8 *buffer, CsrSize length)
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

CsrUint8 *CsrEventCsrUint16CsrUint8Ser(CsrUint8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint16CsrUint8 *primitive = (CsrEventCsrUint16CsrUint8 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value1);
    CsrUint8Ser(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint16CsrUint8Des(CsrUint8 *buffer, CsrSize length)
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

CsrUint8 *CsrEventCsrUint16CsrUint16Ser(CsrUint8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint16CsrUint16 *primitive = (CsrEventCsrUint16CsrUint16 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value1);
    CsrUint16Ser(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint16CsrUint16Des(CsrUint8 *buffer, CsrSize length)
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

CsrUint8 *CsrEventCsrUint16CsrUint32Ser(CsrUint8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint16CsrUint32 *primitive = (CsrEventCsrUint16CsrUint32 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value1);
    CsrUint32Ser(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint16CsrUint32Des(CsrUint8 *buffer, CsrSize length)
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

CsrUint8 *CsrEventCsrUint16CsrCharStringSer(CsrUint8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint16CsrCharString *primitive = (CsrEventCsrUint16CsrCharString *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value1);
    CsrCharStringSer(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint16CsrCharStringDes(CsrUint8 *buffer, CsrSize length)
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

CsrUint8 *CsrEventCsrUint32CsrUint16Ser(CsrUint8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint32CsrUint16 *primitive = (CsrEventCsrUint32CsrUint16 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint32Ser(ptr, len, primitive->value1);
    CsrUint16Ser(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint32CsrUint16Des(CsrUint8 *buffer, CsrSize length)
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

CsrUint8 *CsrEventCsrUint32CsrCharStringSer(CsrUint8 *ptr, CsrSize *len, void *msg)
{
    CsrEventCsrUint32CsrCharString *primitive = (CsrEventCsrUint32CsrCharString *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint32Ser(ptr, len, primitive->value1);
    CsrCharStringSer(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint32CsrCharStringDes(CsrUint8 *buffer, CsrSize length)
{
    CsrEventCsrUint32CsrCharString *primitive = (CsrEventCsrUint32CsrCharString *) CsrPmemAlloc(sizeof(CsrEventCsrUint32CsrCharString));

    CsrSize offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint32Des(&primitive->value1, buffer, &offset);
    CsrCharStringDes(&primitive->value2, buffer, &offset);

    return primitive;
}
