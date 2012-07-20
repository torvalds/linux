/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/module.h>
#include <linux/slab.h>
#include "csr_pmem.h"
#include "csr_macro.h"
#include "csr_msgconv.h"
#include "csr_wifi_msgconv.h"
#include "csr_wifi_lib.h"

void CsrUint24Des(u32 *v, u8 *buffer, size_t *offset)
{
    u32 val;

    val = ((buffer[(*offset) + 2] << 16) |
           (buffer[(*offset) + 1] << 8) |
           (buffer[(*offset)]));

    *offset += 3;
    *v = val;
}


/* Big endian :e.g WSC, TCLAS */
void CsrUint16DesBigEndian(u16 *v, u8 *buffer, size_t *offset)
{
    u16 val;

    val = (buffer[(*offset)] << 8) | (buffer[(*offset) + 1]);
    *offset += 2;

    *v = val;
}


void CsrUint24DesBigEndian(u32 *v, u8 *buffer, size_t *offset)
{
    u32 val;

    val = ((buffer[(*offset)] << 16) |
           (buffer[(*offset) + 1] << 8) |
           (buffer[(*offset) + 2]));

    *offset += 3;
    *v = val;
}


void CsrUint32DesBigEndian(u32 *v, u8 *buffer, size_t *offset)
{
    u32 val;

    val = ((buffer[(*offset)] << 24) |
           (buffer[(*offset) + 1] << 16) |
           (buffer[(*offset) + 2] << 8) |
           (buffer[(*offset) + 3]));

    *offset += 4;
    *v = val;
}


void CsrUint24Ser(u8 *ptr, size_t *len, u32 v)
{
    ptr[(*len) + 2] = (u8)((v & 0x00ff0000) >> 16);
    ptr[(*len) + 1] = (u8)((v & 0x0000ff00) >> 8);
    ptr[(*len)]     = (u8)((v & 0x000000ff));

    *len += 3;
}


/* Big endian :e.g WSC, TCLAS */
void CsrUint16SerBigEndian(u8 *ptr, size_t *len, u16 v)
{
    ptr[(*len)] = (u8)((v & 0xff00) >> 8);
    ptr[(*len) + 1] = (u8)((v & 0x00ff));

    *len += 2;
}


void CsrUint32SerBigEndian(u8 *ptr, size_t *len, u32 v)
{
    ptr[(*len)] = (u8)((v & 0xff000000) >> 24);
    ptr[(*len) + 1] = (u8)((v & 0x00ff0000) >> 16);
    ptr[(*len) + 2] = (u8)((v & 0x0000ff00) >> 8);
    ptr[(*len) + 3] = (u8)((v & 0x000000ff));

    *len += 4;
}


void CsrUint24SerBigEndian(u8 *ptr, size_t *len, u32 v)
{
    ptr[(*len)] = (u8)((v & 0x00ff0000) >> 16);
    ptr[(*len) + 1] = (u8)((v & 0x0000ff00) >> 8);
    ptr[(*len) + 2] = (u8)((v & 0x000000ff));

    *len += 3;
}


size_t CsrWifiEventSizeof(void *msg)
{
    return 2;
}
EXPORT_SYMBOL_GPL(CsrWifiEventSizeof);

u8* CsrWifiEventSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiFsmEvent *primitive = (CsrWifiFsmEvent *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->type);
    return(ptr);
}
EXPORT_SYMBOL_GPL(CsrWifiEventSer);

void* CsrWifiEventDes(u8 *buffer, size_t length)
{
    CsrWifiFsmEvent *primitive = kmalloc(sizeof(CsrWifiFsmEvent), GFP_KERNEL);
    size_t offset = 0;
    CsrUint16Des(&primitive->type, buffer, &offset);

    return primitive;
}
EXPORT_SYMBOL_GPL(CsrWifiEventDes);

size_t CsrWifiEventCsrUint8Sizeof(void *msg)
{
    return 3;
}
EXPORT_SYMBOL_GPL(CsrWifiEventCsrUint8Sizeof);

u8* CsrWifiEventCsrUint8Ser(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiEventCsrUint8 *primitive = (CsrWifiEventCsrUint8 *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint8Ser(ptr, len, primitive->value);
    return(ptr);
}
EXPORT_SYMBOL_GPL(CsrWifiEventCsrUint8Ser);


void* CsrWifiEventCsrUint8Des(u8 *buffer, size_t length)
{
    CsrWifiEventCsrUint8 *primitive = kmalloc(sizeof(CsrWifiEventCsrUint8), GFP_KERNEL);

    size_t offset = 0;
    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint8Des(&primitive->value, buffer, &offset);

    return primitive;
}
EXPORT_SYMBOL_GPL(CsrWifiEventCsrUint8Des);


size_t CsrWifiEventCsrUint16Sizeof(void *msg)
{
    return 4;
}
EXPORT_SYMBOL_GPL(CsrWifiEventCsrUint16Sizeof);


u8* CsrWifiEventCsrUint16Ser(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiEventCsrUint16 *primitive = (CsrWifiEventCsrUint16 *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, primitive->value);
    return(ptr);
}
EXPORT_SYMBOL_GPL(CsrWifiEventCsrUint16Ser);

void* CsrWifiEventCsrUint16Des(u8 *buffer, size_t length)
{
    CsrWifiEventCsrUint16 *primitive = kmalloc(sizeof(CsrWifiEventCsrUint16), GFP_KERNEL);

    size_t offset = 0;
    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des(&primitive->value, buffer, &offset);

    return primitive;
}
EXPORT_SYMBOL_GPL(CsrWifiEventCsrUint16Des);


size_t CsrWifiEventCsrUint32Sizeof(void *msg)
{
    return 6;
}
EXPORT_SYMBOL_GPL(CsrWifiEventCsrUint32Sizeof);

u8* CsrWifiEventCsrUint32Ser(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiEventCsrUint32 *primitive = (CsrWifiEventCsrUint32 *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint32Ser(ptr, len, primitive->value);
    return(ptr);
}
EXPORT_SYMBOL_GPL(CsrWifiEventCsrUint32Ser);


void* CsrWifiEventCsrUint32Des(u8 *buffer, size_t length)
{
    CsrWifiEventCsrUint32 *primitive = kmalloc(sizeof(CsrWifiEventCsrUint32), GFP_KERNEL);

    size_t offset = 0;
    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint32Des(&primitive->value, buffer, &offset);

    return primitive;
}
EXPORT_SYMBOL_GPL(CsrWifiEventCsrUint32Des);

size_t CsrWifiEventCsrUint16CsrUint8Sizeof(void *msg)
{
    return 5;
}
EXPORT_SYMBOL_GPL(CsrWifiEventCsrUint16CsrUint8Sizeof);

u8* CsrWifiEventCsrUint16CsrUint8Ser(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiEventCsrUint16CsrUint8 *primitive = (CsrWifiEventCsrUint16CsrUint8 *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, primitive->value16);
    CsrUint8Ser(ptr, len, primitive->value8);
    return(ptr);
}
EXPORT_SYMBOL_GPL(CsrWifiEventCsrUint16CsrUint8Ser);


void* CsrWifiEventCsrUint16CsrUint8Des(u8 *buffer, size_t length)
{
    CsrWifiEventCsrUint16CsrUint8 *primitive = kmalloc(sizeof(CsrWifiEventCsrUint16CsrUint8), GFP_KERNEL);

    size_t offset = 0;
    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des(&primitive->value16, buffer, &offset);
    CsrUint8Des(&primitive->value8, buffer, &offset);

    return primitive;
}
EXPORT_SYMBOL_GPL(CsrWifiEventCsrUint16CsrUint8Des);


