/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/module.h>
#include <linux/slab.h>
#include "csr_prim_defs.h"
#include "csr_msgconv.h"
#include "csr_macro.h"
#include "csr_pmem.h"
#include "csr_lib.h"

void CsrUint8Des(u8 *value, u8 *buffer, size_t *offset)
{
    *value = buffer[*offset];
    *offset += sizeof(*value);
}
EXPORT_SYMBOL_GPL(CsrUint8Des);

void CsrUint16Des(u16 *value, u8 *buffer, size_t *offset)
{
    *value = (buffer[*offset + 0] << 0) |
             (buffer[*offset + 1] << 8);
    *offset += sizeof(*value);
}
EXPORT_SYMBOL_GPL(CsrUint16Des);

void CsrUint32Des(u32 *value, u8 *buffer, size_t *offset)
{
    *value = (buffer[*offset + 0] << 0) |
             (buffer[*offset + 1] << 8) |
             (buffer[*offset + 2] << 16) |
             (buffer[*offset + 3] << 24);
    *offset += sizeof(*value);
}
EXPORT_SYMBOL_GPL(CsrUint32Des);

void CsrMemCpyDes(void *value, u8 *buffer, size_t *offset, size_t length)
{
    memcpy(value, &buffer[*offset], length);
    *offset += length;
}
EXPORT_SYMBOL_GPL(CsrMemCpyDes);

void CsrCharStringDes(char **value, u8 *buffer, size_t *offset)
{
    *value = kstrdup((char *) &buffer[*offset], GFP_KERNEL);
    *offset += strlen(*value) + 1;
}
EXPORT_SYMBOL_GPL(CsrCharStringDes);

void CsrUtf8StringDes(u8 **value, u8 *buffer, size_t *offset)
{
    *value = (u8 *)kstrdup((char *) &buffer[*offset], GFP_KERNEL);
    *offset += strlen((char *) *value) + 1;
}

void CsrUtf16StringDes(u16 **value, u8 *buffer, size_t *offset)
{
    u32 length, i;

    CsrUint32Des(&length, buffer, offset);

    *value = kmalloc(length * sizeof(**value), GFP_KERNEL);
    for (i = 0; i < length; i++)
    {
        CsrUint16Des(&(*value)[i], buffer, offset);
    }
}

void CsrSizeDes(size_t *value, u8 *buffer, size_t *offset)
{
    *value = (buffer[*offset + 0] << 0) |
             (buffer[*offset + 1] << 8) |
             (buffer[*offset + 2] << 16) |
             (buffer[*offset + 3] << 24);
    *offset += sizeof(*value);
}

void CsrVoidPtrDes(void **value, u8 *buffer, size_t *offset)
{
    CsrSizeDes((size_t *) value, buffer, offset);
}

void CsrUint8Ser(u8 *buffer, size_t *offset, u8 value)
{
    buffer[*offset] = value;
    *offset += sizeof(value);
}
EXPORT_SYMBOL_GPL(CsrUint8Ser);

void CsrUint16Ser(u8 *buffer, size_t *offset, u16 value)
{
    buffer[*offset + 0] = (u8) ((value >> 0) & 0xFF);
    buffer[*offset + 1] = (u8) ((value >> 8) & 0xFF);
    *offset += sizeof(value);
}
EXPORT_SYMBOL_GPL(CsrUint16Ser);

void CsrUint32Ser(u8 *buffer, size_t *offset, u32 value)
{
    buffer[*offset + 0] = (u8) ((value >> 0) & 0xFF);
    buffer[*offset + 1] = (u8) ((value >> 8) & 0xFF);
    buffer[*offset + 2] = (u8) ((value >> 16) & 0xFF);
    buffer[*offset + 3] = (u8) ((value >> 24) & 0xFF);
    *offset += sizeof(value);
}
EXPORT_SYMBOL_GPL(CsrUint32Ser);

void CsrMemCpySer(u8 *buffer, size_t *offset, const void *value, size_t length)
{
    memcpy(&buffer[*offset], value, length);
    *offset += length;
}
EXPORT_SYMBOL_GPL(CsrMemCpySer);

void CsrCharStringSer(u8 *buffer, size_t *offset, const char *value)
{
    if (value)
    {
        strcpy(((char *) &buffer[*offset]), value);
        *offset += strlen(value) + 1;
    }
    else
    {
        CsrUint8Ser(buffer, offset, 0);
    }
}
EXPORT_SYMBOL_GPL(CsrCharStringSer);

void CsrUtf8StringSer(u8 *buffer, size_t *offset, const u8 *value)
{
    CsrCharStringSer(buffer, offset, (char *) value);
}

void CsrUtf16StringSer(u8 *buffer, size_t *offset, const u16 *value)
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

void CsrSizeSer(u8 *buffer, size_t *offset, size_t value)
{
    buffer[*offset + 0] = (u8) ((value >> 0) & 0xFF);
    buffer[*offset + 1] = (u8) ((value >> 8) & 0xFF);
    buffer[*offset + 2] = (u8) ((value >> 16) & 0xFF);
    buffer[*offset + 3] = (u8) ((value >> 24) & 0xFF);
    *offset += sizeof(value);
}

void CsrVoidPtrSer(u8 *buffer, size_t *offset, void *ptr)
{
    CsrSizeSer(buffer, offset, (size_t) ptr);
}

u32 CsrCharStringSerLen(const char *str)
{
    if (str)
    {
        return (u32)(strlen(str) + sizeof(*str));
    }
    else
    {
        return sizeof(*str);
    }
}

u32 CsrUtf8StringSerLen(const u8 *str)
{
    if (str)
    {
        return (u32)(strlen((char *) str) + sizeof(*str));
    }
    else
    {
        return sizeof(*str);
    }
}

u32 CsrUtf16StringSerLen(const u16 *str)
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

size_t CsrEventSizeof(void *msg)
{
    return 2;
}

u8 *CsrEventSer(u8 *ptr, size_t *len, void *msg)
{
    CsrEvent *primitive = (CsrEvent *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    return ptr;
}

void *CsrEventDes(u8 *buffer, size_t length)
{
    CsrEvent *primitive = kmalloc(sizeof(CsrEvent), GFP_KERNEL);
    size_t offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);

    return primitive;
}

size_t CsrEventCsrUint8Sizeof(void *msg)
{
    return 3;
}

u8 *CsrEventCsrUint8Ser(u8 *ptr, size_t *len, void *msg)
{
    CsrEventCsrUint8 *primitive = (CsrEventCsrUint8 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint8Ser(ptr, len, primitive->value);
    return ptr;
}

void *CsrEventCsrUint8Des(u8 *buffer, size_t length)
{
    CsrEventCsrUint8 *primitive = kmalloc(sizeof(CsrEventCsrUint8), GFP_KERNEL);

    size_t offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint8Des(&primitive->value, buffer, &offset);

    return primitive;
}

size_t CsrEventCsrUint16Sizeof(void *msg)
{
    return 4;
}

u8 *CsrEventCsrUint16Ser(u8 *ptr, size_t *len, void *msg)
{
    CsrEventCsrUint16 *primitive = (CsrEventCsrUint16 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value);
    return ptr;
}

void *CsrEventCsrUint16Des(u8 *buffer, size_t length)
{
    CsrEventCsrUint16 *primitive = kmalloc(sizeof(CsrEventCsrUint16), GFP_KERNEL);

    size_t offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint16Des(&primitive->value, buffer, &offset);

    return primitive;
}

size_t CsrEventCsrUint32Sizeof(void *msg)
{
    return 6;
}

u8 *CsrEventCsrUint32Ser(u8 *ptr, size_t *len, void *msg)
{
    CsrEventCsrUint32 *primitive = (CsrEventCsrUint32 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint32Ser(ptr, len, primitive->value);
    return ptr;
}

void *CsrEventCsrUint32Des(u8 *buffer, size_t length)
{
    CsrEventCsrUint32 *primitive = kmalloc(sizeof(CsrEventCsrUint32), GFP_KERNEL);

    size_t offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint32Des(&primitive->value, buffer, &offset);

    return primitive;
}

size_t CsrEventCsrUint16CsrUint8Sizeof(void *msg)
{
    return 5;
}

u8 *CsrEventCsrUint16CsrUint8Ser(u8 *ptr, size_t *len, void *msg)
{
    CsrEventCsrUint16CsrUint8 *primitive = (CsrEventCsrUint16CsrUint8 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value1);
    CsrUint8Ser(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint16CsrUint8Des(u8 *buffer, size_t length)
{
    CsrEventCsrUint16CsrUint8 *primitive = kmalloc(sizeof(CsrEventCsrUint16CsrUint8), GFP_KERNEL);

    size_t offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint16Des(&primitive->value1, buffer, &offset);
    CsrUint8Des(&primitive->value2, buffer, &offset);

    return primitive;
}

size_t CsrEventCsrUint16CsrUint16Sizeof(void *msg)
{
    return 6;
}

u8 *CsrEventCsrUint16CsrUint16Ser(u8 *ptr, size_t *len, void *msg)
{
    CsrEventCsrUint16CsrUint16 *primitive = (CsrEventCsrUint16CsrUint16 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value1);
    CsrUint16Ser(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint16CsrUint16Des(u8 *buffer, size_t length)
{
    CsrEventCsrUint16CsrUint16 *primitive = kmalloc(sizeof(CsrEventCsrUint16CsrUint16), GFP_KERNEL);

    size_t offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint16Des(&primitive->value1, buffer, &offset);
    CsrUint16Des(&primitive->value2, buffer, &offset);

    return primitive;
}

size_t CsrEventCsrUint16CsrUint32Sizeof(void *msg)
{
    return 8;
}

u8 *CsrEventCsrUint16CsrUint32Ser(u8 *ptr, size_t *len, void *msg)
{
    CsrEventCsrUint16CsrUint32 *primitive = (CsrEventCsrUint16CsrUint32 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value1);
    CsrUint32Ser(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint16CsrUint32Des(u8 *buffer, size_t length)
{
    CsrEventCsrUint16CsrUint32 *primitive = kmalloc(sizeof(CsrEventCsrUint16CsrUint32), GFP_KERNEL);

    size_t offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint16Des(&primitive->value1, buffer, &offset);
    CsrUint32Des(&primitive->value2, buffer, &offset);

    return primitive;
}

size_t CsrEventCsrUint16CsrCharStringSizeof(void *msg)
{
    CsrEventCsrUint16CsrCharString *primitive = (CsrEventCsrUint16CsrCharString *) msg;
    return 4 + strlen(primitive->value2) + 1;
}

u8 *CsrEventCsrUint16CsrCharStringSer(u8 *ptr, size_t *len, void *msg)
{
    CsrEventCsrUint16CsrCharString *primitive = (CsrEventCsrUint16CsrCharString *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint16Ser(ptr, len, primitive->value1);
    CsrCharStringSer(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint16CsrCharStringDes(u8 *buffer, size_t length)
{
    CsrEventCsrUint16CsrCharString *primitive = kmalloc(sizeof(CsrEventCsrUint16CsrCharString), GFP_KERNEL);

    size_t offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint16Des(&primitive->value1, buffer, &offset);
    CsrCharStringDes(&primitive->value2, buffer, &offset);

    return primitive;
}

size_t CsrEventCsrUint32CsrUint16Sizeof(void *msg)
{
    return 8;
}

u8 *CsrEventCsrUint32CsrUint16Ser(u8 *ptr, size_t *len, void *msg)
{
    CsrEventCsrUint32CsrUint16 *primitive = (CsrEventCsrUint32CsrUint16 *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint32Ser(ptr, len, primitive->value1);
    CsrUint16Ser(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint32CsrUint16Des(u8 *buffer, size_t length)
{
    CsrEventCsrUint32CsrUint16 *primitive = kmalloc(sizeof(CsrEventCsrUint32CsrUint16), GFP_KERNEL);

    size_t offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint32Des(&primitive->value1, buffer, &offset);
    CsrUint16Des(&primitive->value2, buffer, &offset);

    return primitive;
}

size_t CsrEventCsrUint32CsrCharStringSizeof(void *msg)
{
    CsrEventCsrUint32CsrCharString *primitive = (CsrEventCsrUint32CsrCharString *) msg;
    return 6 + strlen(primitive->value2) + 1;
}

u8 *CsrEventCsrUint32CsrCharStringSer(u8 *ptr, size_t *len, void *msg)
{
    CsrEventCsrUint32CsrCharString *primitive = (CsrEventCsrUint32CsrCharString *) msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    CsrUint32Ser(ptr, len, primitive->value1);
    CsrCharStringSer(ptr, len, primitive->value2);
    return ptr;
}

void *CsrEventCsrUint32CsrCharStringDes(u8 *buffer, size_t length)
{
    CsrEventCsrUint32CsrCharString *primitive = kmalloc(sizeof(CsrEventCsrUint32CsrCharString), GFP_KERNEL);

    size_t offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);
    CsrUint32Des(&primitive->value1, buffer, &offset);
    CsrCharStringDes(&primitive->value2, buffer, &offset);

    return primitive;
}
