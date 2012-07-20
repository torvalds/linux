/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#include "csr_pmem.h"
#include "csr_msgconv.h"
#include "csr_unicode.h"


#include "csr_wifi_router_prim.h"
#include "csr_wifi_router_serialize.h"

void CsrWifiRouterPfree(void *ptr)
{
    CsrPmemFree(ptr);
}


CsrSize CsrWifiRouterMaPacketSubscribeReqSizeof(void *msg)
{
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 12) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 1; /* CsrWifiRouterEncapsulation primitive->encapsulation */
    bufferSize += 2; /* u16 primitive->protocol */
    bufferSize += 4; /* u32 primitive->oui */
    return bufferSize;
}


u8* CsrWifiRouterMaPacketSubscribeReqSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiRouterMaPacketSubscribeReq *primitive = (CsrWifiRouterMaPacketSubscribeReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint8Ser(ptr, len, (u8) primitive->encapsulation);
    CsrUint16Ser(ptr, len, (u16) primitive->protocol);
    CsrUint32Ser(ptr, len, (u32) primitive->oui);
    return(ptr);
}


void* CsrWifiRouterMaPacketSubscribeReqDes(u8 *buffer, CsrSize length)
{
    CsrWifiRouterMaPacketSubscribeReq *primitive = (CsrWifiRouterMaPacketSubscribeReq *) CsrPmemAlloc(sizeof(CsrWifiRouterMaPacketSubscribeReq));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->encapsulation, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->protocol, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->oui, buffer, &offset);

    return primitive;
}


CsrSize CsrWifiRouterMaPacketReqSizeof(void *msg)
{
    CsrWifiRouterMaPacketReq *primitive = (CsrWifiRouterMaPacketReq *) msg;
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 20) */
    bufferSize += 2;                      /* u16 primitive->interfaceTag */
    bufferSize += 1;                      /* u8 primitive->subscriptionHandle */
    bufferSize += 2;                      /* u16 primitive->frameLength */
    bufferSize += primitive->frameLength; /* u8 primitive->frame */
    bufferSize += 4;                      /* CsrWifiRouterFrameFreeFunction primitive->freeFunction */
    bufferSize += 2;                      /* CsrWifiRouterPriority primitive->priority */
    bufferSize += 4;                      /* u32 primitive->hostTag */
    bufferSize += 1;                      /* CsrBool primitive->cfmRequested */
    return bufferSize;
}


u8* CsrWifiRouterMaPacketReqSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiRouterMaPacketReq *primitive = (CsrWifiRouterMaPacketReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint8Ser(ptr, len, (u8) primitive->subscriptionHandle);
    CsrUint16Ser(ptr, len, (u16) primitive->frameLength);
    if (primitive->frameLength)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->frame, ((u16) (primitive->frameLength)));
    }
    CsrUint32Ser(ptr, len, 0); /* Special for Function Pointers... primitive->freeFunction */
    CsrUint16Ser(ptr, len, (u16) primitive->priority);
    CsrUint32Ser(ptr, len, (u32) primitive->hostTag);
    CsrUint8Ser(ptr, len, (u8) primitive->cfmRequested);
    return(ptr);
}


void* CsrWifiRouterMaPacketReqDes(u8 *buffer, CsrSize length)
{
    CsrWifiRouterMaPacketReq *primitive = (CsrWifiRouterMaPacketReq *) CsrPmemAlloc(sizeof(CsrWifiRouterMaPacketReq));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->subscriptionHandle, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->frameLength, buffer, &offset);
    if (primitive->frameLength)
    {
        primitive->frame = (u8 *)CsrPmemAlloc(primitive->frameLength);
        CsrMemCpyDes(primitive->frame, buffer, &offset, ((u16) (primitive->frameLength)));
    }
    else
    {
        primitive->frame = NULL;
    }
    primitive->freeFunction = NULL; /* Special for Function Pointers... */
    offset += 4;
    CsrUint16Des((u16 *) &primitive->priority, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->hostTag, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->cfmRequested, buffer, &offset);

    return primitive;
}


void CsrWifiRouterMaPacketReqSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterMaPacketReq *primitive = (CsrWifiRouterMaPacketReq *) voidPrimitivePointer;
    CsrPmemFree(primitive->frame);
    CsrPmemFree(primitive);
}


CsrSize CsrWifiRouterMaPacketResSizeof(void *msg)
{
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 8) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 1; /* u8 primitive->subscriptionHandle */
    bufferSize += 2; /* CsrResult primitive->result */
    return bufferSize;
}


u8* CsrWifiRouterMaPacketResSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiRouterMaPacketRes *primitive = (CsrWifiRouterMaPacketRes *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint8Ser(ptr, len, (u8) primitive->subscriptionHandle);
    CsrUint16Ser(ptr, len, (u16) primitive->result);
    return(ptr);
}


void* CsrWifiRouterMaPacketResDes(u8 *buffer, CsrSize length)
{
    CsrWifiRouterMaPacketRes *primitive = (CsrWifiRouterMaPacketRes *) CsrPmemAlloc(sizeof(CsrWifiRouterMaPacketRes));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->subscriptionHandle, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->result, buffer, &offset);

    return primitive;
}


CsrSize CsrWifiRouterMaPacketCancelReqSizeof(void *msg)
{
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 17) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 4; /* u32 primitive->hostTag */
    bufferSize += 2; /* CsrWifiRouterPriority primitive->priority */
    bufferSize += 6; /* u8 primitive->peerMacAddress.a[6] */
    return bufferSize;
}


u8* CsrWifiRouterMaPacketCancelReqSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiRouterMaPacketCancelReq *primitive = (CsrWifiRouterMaPacketCancelReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint32Ser(ptr, len, (u32) primitive->hostTag);
    CsrUint16Ser(ptr, len, (u16) primitive->priority);
    CsrMemCpySer(ptr, len, (const void *) primitive->peerMacAddress.a, ((u16) (6)));
    return(ptr);
}


void* CsrWifiRouterMaPacketCancelReqDes(u8 *buffer, CsrSize length)
{
    CsrWifiRouterMaPacketCancelReq *primitive = (CsrWifiRouterMaPacketCancelReq *) CsrPmemAlloc(sizeof(CsrWifiRouterMaPacketCancelReq));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->hostTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->priority, buffer, &offset);
    CsrMemCpyDes(primitive->peerMacAddress.a, buffer, &offset, ((u16) (6)));

    return primitive;
}


CsrSize CsrWifiRouterMaPacketSubscribeCfmSizeof(void *msg)
{
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 10) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 1; /* u8 primitive->subscriptionHandle */
    bufferSize += 2; /* CsrResult primitive->status */
    bufferSize += 2; /* u16 primitive->allocOffset */
    return bufferSize;
}


u8* CsrWifiRouterMaPacketSubscribeCfmSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiRouterMaPacketSubscribeCfm *primitive = (CsrWifiRouterMaPacketSubscribeCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint8Ser(ptr, len, (u8) primitive->subscriptionHandle);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    CsrUint16Ser(ptr, len, (u16) primitive->allocOffset);
    return(ptr);
}


void* CsrWifiRouterMaPacketSubscribeCfmDes(u8 *buffer, CsrSize length)
{
    CsrWifiRouterMaPacketSubscribeCfm *primitive = (CsrWifiRouterMaPacketSubscribeCfm *) CsrPmemAlloc(sizeof(CsrWifiRouterMaPacketSubscribeCfm));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->subscriptionHandle, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->allocOffset, buffer, &offset);

    return primitive;
}


CsrSize CsrWifiRouterMaPacketUnsubscribeCfmSizeof(void *msg)
{
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 7) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiRouterMaPacketUnsubscribeCfmSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiRouterMaPacketUnsubscribeCfm *primitive = (CsrWifiRouterMaPacketUnsubscribeCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiRouterMaPacketUnsubscribeCfmDes(u8 *buffer, CsrSize length)
{
    CsrWifiRouterMaPacketUnsubscribeCfm *primitive = (CsrWifiRouterMaPacketUnsubscribeCfm *) CsrPmemAlloc(sizeof(CsrWifiRouterMaPacketUnsubscribeCfm));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


CsrSize CsrWifiRouterMaPacketCfmSizeof(void *msg)
{
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 13) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrResult primitive->result */
    bufferSize += 4; /* u32 primitive->hostTag */
    bufferSize += 2; /* u16 primitive->rate */
    return bufferSize;
}


u8* CsrWifiRouterMaPacketCfmSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiRouterMaPacketCfm *primitive = (CsrWifiRouterMaPacketCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->result);
    CsrUint32Ser(ptr, len, (u32) primitive->hostTag);
    CsrUint16Ser(ptr, len, (u16) primitive->rate);
    return(ptr);
}


void* CsrWifiRouterMaPacketCfmDes(u8 *buffer, CsrSize length)
{
    CsrWifiRouterMaPacketCfm *primitive = (CsrWifiRouterMaPacketCfm *) CsrPmemAlloc(sizeof(CsrWifiRouterMaPacketCfm));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->result, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->hostTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->rate, buffer, &offset);

    return primitive;
}


CsrSize CsrWifiRouterMaPacketIndSizeof(void *msg)
{
    CsrWifiRouterMaPacketInd *primitive = (CsrWifiRouterMaPacketInd *) msg;
    CsrSize bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 21) */
    bufferSize += 2;                      /* u16 primitive->interfaceTag */
    bufferSize += 1;                      /* u8 primitive->subscriptionHandle */
    bufferSize += 2;                      /* CsrResult primitive->result */
    bufferSize += 2;                      /* u16 primitive->frameLength */
    bufferSize += primitive->frameLength; /* u8 primitive->frame */
    bufferSize += 4;                      /* CsrWifiRouterFrameFreeFunction primitive->freeFunction */
    bufferSize += 2;                      /* s16 primitive->rssi */
    bufferSize += 2;                      /* s16 primitive->snr */
    bufferSize += 2;                      /* u16 primitive->rate */
    return bufferSize;
}


u8* CsrWifiRouterMaPacketIndSer(u8 *ptr, CsrSize *len, void *msg)
{
    CsrWifiRouterMaPacketInd *primitive = (CsrWifiRouterMaPacketInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint8Ser(ptr, len, (u8) primitive->subscriptionHandle);
    CsrUint16Ser(ptr, len, (u16) primitive->result);
    CsrUint16Ser(ptr, len, (u16) primitive->frameLength);
    if (primitive->frameLength)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->frame, ((u16) (primitive->frameLength)));
    }
    CsrUint32Ser(ptr, len, 0); /* Special for Function Pointers... primitive->freeFunction */
    CsrUint16Ser(ptr, len, (u16) primitive->rssi);
    CsrUint16Ser(ptr, len, (u16) primitive->snr);
    CsrUint16Ser(ptr, len, (u16) primitive->rate);
    return(ptr);
}


void* CsrWifiRouterMaPacketIndDes(u8 *buffer, CsrSize length)
{
    CsrWifiRouterMaPacketInd *primitive = (CsrWifiRouterMaPacketInd *) CsrPmemAlloc(sizeof(CsrWifiRouterMaPacketInd));
    CsrSize offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->subscriptionHandle, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->result, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->frameLength, buffer, &offset);
    if (primitive->frameLength)
    {
        primitive->frame = (u8 *)CsrPmemAlloc(primitive->frameLength);
        CsrMemCpyDes(primitive->frame, buffer, &offset, ((u16) (primitive->frameLength)));
    }
    else
    {
        primitive->frame = NULL;
    }
    primitive->freeFunction = NULL; /* Special for Function Pointers... */
    offset += 4;
    CsrUint16Des((u16 *) &primitive->rssi, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->snr, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->rate, buffer, &offset);

    return primitive;
}


void CsrWifiRouterMaPacketIndSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterMaPacketInd *primitive = (CsrWifiRouterMaPacketInd *) voidPrimitivePointer;
    CsrPmemFree(primitive->frame);
    CsrPmemFree(primitive);
}


