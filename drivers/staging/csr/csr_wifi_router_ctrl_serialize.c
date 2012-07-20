/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */
#include <linux/string.h>
#include <linux/slab.h>
#include "csr_pmem.h"
#include "csr_msgconv.h"
#include "csr_unicode.h"


#include "csr_wifi_router_ctrl_prim.h"
#include "csr_wifi_router_ctrl_serialize.h"

void CsrWifiRouterCtrlPfree(void *ptr)
{
    kfree(ptr);
}


size_t CsrWifiRouterCtrlConfigurePowerModeReqSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 8) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* CsrWifiRouterCtrlLowPowerMode primitive->mode */
    bufferSize += 1; /* u8 primitive->wakeHost */
    return bufferSize;
}


u8* CsrWifiRouterCtrlConfigurePowerModeReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlConfigurePowerModeReq *primitive = (CsrWifiRouterCtrlConfigurePowerModeReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->mode);
    CsrUint8Ser(ptr, len, (u8) primitive->wakeHost);
    return(ptr);
}


void* CsrWifiRouterCtrlConfigurePowerModeReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlConfigurePowerModeReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlConfigurePowerModeReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->mode, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->wakeHost, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlHipReqSizeof(void *msg)
{
    CsrWifiRouterCtrlHipReq *primitive = (CsrWifiRouterCtrlHipReq *) msg;
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 12) */
    bufferSize += 2;                            /* u16 primitive->mlmeCommandLength */
    bufferSize += primitive->mlmeCommandLength; /* u8 primitive->mlmeCommand */
    bufferSize += 2;                            /* u16 primitive->dataRef1Length */
    bufferSize += primitive->dataRef1Length;    /* u8 primitive->dataRef1 */
    bufferSize += 2;                            /* u16 primitive->dataRef2Length */
    bufferSize += primitive->dataRef2Length;    /* u8 primitive->dataRef2 */
    return bufferSize;
}


u8* CsrWifiRouterCtrlHipReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlHipReq *primitive = (CsrWifiRouterCtrlHipReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->mlmeCommandLength);
    if (primitive->mlmeCommandLength)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->mlmeCommand, ((u16) (primitive->mlmeCommandLength)));
    }
    CsrUint16Ser(ptr, len, (u16) primitive->dataRef1Length);
    if (primitive->dataRef1Length)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->dataRef1, ((u16) (primitive->dataRef1Length)));
    }
    CsrUint16Ser(ptr, len, (u16) primitive->dataRef2Length);
    if (primitive->dataRef2Length)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->dataRef2, ((u16) (primitive->dataRef2Length)));
    }
    return(ptr);
}


void* CsrWifiRouterCtrlHipReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlHipReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlHipReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->mlmeCommandLength, buffer, &offset);
    if (primitive->mlmeCommandLength)
    {
        primitive->mlmeCommand = kmalloc(primitive->mlmeCommandLength, GFP_KERNEL);
        CsrMemCpyDes(primitive->mlmeCommand, buffer, &offset, ((u16) (primitive->mlmeCommandLength)));
    }
    else
    {
        primitive->mlmeCommand = NULL;
    }
    CsrUint16Des((u16 *) &primitive->dataRef1Length, buffer, &offset);
    if (primitive->dataRef1Length)
    {
        primitive->dataRef1 = kmalloc(primitive->dataRef1Length, GFP_KERNEL);
        CsrMemCpyDes(primitive->dataRef1, buffer, &offset, ((u16) (primitive->dataRef1Length)));
    }
    else
    {
        primitive->dataRef1 = NULL;
    }
    CsrUint16Des((u16 *) &primitive->dataRef2Length, buffer, &offset);
    if (primitive->dataRef2Length)
    {
        primitive->dataRef2 = kmalloc(primitive->dataRef2Length, GFP_KERNEL);
        CsrMemCpyDes(primitive->dataRef2, buffer, &offset, ((u16) (primitive->dataRef2Length)));
    }
    else
    {
        primitive->dataRef2 = NULL;
    }

    return primitive;
}


void CsrWifiRouterCtrlHipReqSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterCtrlHipReq *primitive = (CsrWifiRouterCtrlHipReq *) voidPrimitivePointer;
    kfree(primitive->mlmeCommand);
    kfree(primitive->dataRef1);
    kfree(primitive->dataRef2);
    kfree(primitive);
}


size_t CsrWifiRouterCtrlMediaStatusReqSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 8) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 1; /* CsrWifiRouterCtrlMediaStatus primitive->mediaStatus */
    return bufferSize;
}


u8* CsrWifiRouterCtrlMediaStatusReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlMediaStatusReq *primitive = (CsrWifiRouterCtrlMediaStatusReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint8Ser(ptr, len, (u8) primitive->mediaStatus);
    return(ptr);
}


void* CsrWifiRouterCtrlMediaStatusReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlMediaStatusReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlMediaStatusReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->mediaStatus, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlMulticastAddressResSizeof(void *msg)
{
    CsrWifiRouterCtrlMulticastAddressRes *primitive = (CsrWifiRouterCtrlMulticastAddressRes *) msg;
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 17) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* CsrResult primitive->status */
    bufferSize += 1; /* CsrWifiRouterCtrlListAction primitive->action */
    bufferSize += 1; /* u8 primitive->getAddressesCount */
    {
        u16 i1;
        for (i1 = 0; i1 < primitive->getAddressesCount; i1++)
        {
            bufferSize += 6; /* u8 primitive->getAddresses[i1].a[6] */
        }
    }
    return bufferSize;
}


u8* CsrWifiRouterCtrlMulticastAddressResSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlMulticastAddressRes *primitive = (CsrWifiRouterCtrlMulticastAddressRes *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    CsrUint8Ser(ptr, len, (u8) primitive->action);
    CsrUint8Ser(ptr, len, (u8) primitive->getAddressesCount);
    {
        u16 i1;
        for (i1 = 0; i1 < primitive->getAddressesCount; i1++)
        {
            CsrMemCpySer(ptr, len, (const void *) primitive->getAddresses[i1].a, ((u16) (6)));
        }
    }
    return(ptr);
}


void* CsrWifiRouterCtrlMulticastAddressResDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlMulticastAddressRes *primitive = kmalloc(sizeof(CsrWifiRouterCtrlMulticastAddressRes), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->action, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->getAddressesCount, buffer, &offset);
    primitive->getAddresses = NULL;
    if (primitive->getAddressesCount)
    {
        primitive->getAddresses = kmalloc(sizeof(CsrWifiMacAddress) * primitive->getAddressesCount, GFP_KERNEL);
    }
    {
        u16 i1;
        for (i1 = 0; i1 < primitive->getAddressesCount; i1++)
        {
            CsrMemCpyDes(primitive->getAddresses[i1].a, buffer, &offset, ((u16) (6)));
        }
    }

    return primitive;
}


void CsrWifiRouterCtrlMulticastAddressResSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterCtrlMulticastAddressRes *primitive = (CsrWifiRouterCtrlMulticastAddressRes *) voidPrimitivePointer;
    kfree(primitive->getAddresses);
    kfree(primitive);
}


size_t CsrWifiRouterCtrlPortConfigureReqSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 18) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* CsrWifiRouterCtrlPortAction primitive->uncontrolledPortAction */
    bufferSize += 2; /* CsrWifiRouterCtrlPortAction primitive->controlledPortAction */
    bufferSize += 6; /* u8 primitive->macAddress.a[6] */
    bufferSize += 1; /* u8 primitive->setProtection */
    return bufferSize;
}


u8* CsrWifiRouterCtrlPortConfigureReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlPortConfigureReq *primitive = (CsrWifiRouterCtrlPortConfigureReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->uncontrolledPortAction);
    CsrUint16Ser(ptr, len, (u16) primitive->controlledPortAction);
    CsrMemCpySer(ptr, len, (const void *) primitive->macAddress.a, ((u16) (6)));
    CsrUint8Ser(ptr, len, (u8) primitive->setProtection);
    return(ptr);
}


void* CsrWifiRouterCtrlPortConfigureReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlPortConfigureReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlPortConfigureReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->uncontrolledPortAction, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->controlledPortAction, buffer, &offset);
    CsrMemCpyDes(primitive->macAddress.a, buffer, &offset, ((u16) (6)));
    CsrUint8Des((u8 *) &primitive->setProtection, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlQosControlReqSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 10) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* CsrWifiRouterCtrlQoSControl primitive->control */
    bufferSize += 1; /* CsrWifiRouterCtrlQueueConfigMask primitive->queueConfig */
    return bufferSize;
}


u8* CsrWifiRouterCtrlQosControlReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlQosControlReq *primitive = (CsrWifiRouterCtrlQosControlReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->control);
    CsrUint8Ser(ptr, len, (u8) primitive->queueConfig);
    return(ptr);
}


void* CsrWifiRouterCtrlQosControlReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlQosControlReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlQosControlReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->control, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->queueConfig, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlSuspendResSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 7) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiRouterCtrlSuspendResSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlSuspendRes *primitive = (CsrWifiRouterCtrlSuspendRes *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiRouterCtrlSuspendResDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlSuspendRes *primitive = kmalloc(sizeof(CsrWifiRouterCtrlSuspendRes), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlTclasAddReqSizeof(void *msg)
{
    CsrWifiRouterCtrlTclasAddReq *primitive = (CsrWifiRouterCtrlTclasAddReq *) msg;
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 10) */
    bufferSize += 2;                      /* u16 primitive->interfaceTag */
    bufferSize += 2;                      /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2;                      /* u16 primitive->tclasLength */
    bufferSize += primitive->tclasLength; /* u8 primitive->tclas */
    return bufferSize;
}


u8* CsrWifiRouterCtrlTclasAddReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlTclasAddReq *primitive = (CsrWifiRouterCtrlTclasAddReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->tclasLength);
    if (primitive->tclasLength)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->tclas, ((u16) (primitive->tclasLength)));
    }
    return(ptr);
}


void* CsrWifiRouterCtrlTclasAddReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlTclasAddReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlTclasAddReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->tclasLength, buffer, &offset);
    if (primitive->tclasLength)
    {
        primitive->tclas = kmalloc(primitive->tclasLength, GFP_KERNEL);
        CsrMemCpyDes(primitive->tclas, buffer, &offset, ((u16) (primitive->tclasLength)));
    }
    else
    {
        primitive->tclas = NULL;
    }

    return primitive;
}


void CsrWifiRouterCtrlTclasAddReqSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterCtrlTclasAddReq *primitive = (CsrWifiRouterCtrlTclasAddReq *) voidPrimitivePointer;
    kfree(primitive->tclas);
    kfree(primitive);
}


size_t CsrWifiRouterCtrlResumeResSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 7) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiRouterCtrlResumeResSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlResumeRes *primitive = (CsrWifiRouterCtrlResumeRes *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiRouterCtrlResumeResDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlResumeRes *primitive = kmalloc(sizeof(CsrWifiRouterCtrlResumeRes), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlTclasDelReqSizeof(void *msg)
{
    CsrWifiRouterCtrlTclasDelReq *primitive = (CsrWifiRouterCtrlTclasDelReq *) msg;
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 10) */
    bufferSize += 2;                      /* u16 primitive->interfaceTag */
    bufferSize += 2;                      /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2;                      /* u16 primitive->tclasLength */
    bufferSize += primitive->tclasLength; /* u8 primitive->tclas */
    return bufferSize;
}


u8* CsrWifiRouterCtrlTclasDelReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlTclasDelReq *primitive = (CsrWifiRouterCtrlTclasDelReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->tclasLength);
    if (primitive->tclasLength)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->tclas, ((u16) (primitive->tclasLength)));
    }
    return(ptr);
}


void* CsrWifiRouterCtrlTclasDelReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlTclasDelReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlTclasDelReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->tclasLength, buffer, &offset);
    if (primitive->tclasLength)
    {
        primitive->tclas = kmalloc(primitive->tclasLength, GFP_KERNEL);
        CsrMemCpyDes(primitive->tclas, buffer, &offset, ((u16) (primitive->tclasLength)));
    }
    else
    {
        primitive->tclas = NULL;
    }

    return primitive;
}


void CsrWifiRouterCtrlTclasDelReqSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterCtrlTclasDelReq *primitive = (CsrWifiRouterCtrlTclasDelReq *) voidPrimitivePointer;
    kfree(primitive->tclas);
    kfree(primitive);
}


size_t CsrWifiRouterCtrlTrafficClassificationReqSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 10) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 1; /* CsrWifiRouterCtrlTrafficType primitive->trafficType */
    bufferSize += 2; /* u16 primitive->period */
    return bufferSize;
}


u8* CsrWifiRouterCtrlTrafficClassificationReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlTrafficClassificationReq *primitive = (CsrWifiRouterCtrlTrafficClassificationReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint8Ser(ptr, len, (u8) primitive->trafficType);
    CsrUint16Ser(ptr, len, (u16) primitive->period);
    return(ptr);
}


void* CsrWifiRouterCtrlTrafficClassificationReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlTrafficClassificationReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlTrafficClassificationReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->trafficType, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->period, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlTrafficConfigReqSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 24) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* CsrWifiRouterCtrlTrafficConfigType primitive->trafficConfigType */
    bufferSize += 2; /* u16 primitive->config.packetFilter */
    bufferSize += 4; /* u32 primitive->config.customFilter.etherType */
    bufferSize += 1; /* u8 primitive->config.customFilter.ipType */
    bufferSize += 4; /* u32 primitive->config.customFilter.udpSourcePort */
    bufferSize += 4; /* u32 primitive->config.customFilter.udpDestPort */
    return bufferSize;
}


u8* CsrWifiRouterCtrlTrafficConfigReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlTrafficConfigReq *primitive = (CsrWifiRouterCtrlTrafficConfigReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->trafficConfigType);
    CsrUint16Ser(ptr, len, (u16) primitive->config.packetFilter);
    CsrUint32Ser(ptr, len, (u32) primitive->config.customFilter.etherType);
    CsrUint8Ser(ptr, len, (u8) primitive->config.customFilter.ipType);
    CsrUint32Ser(ptr, len, (u32) primitive->config.customFilter.udpSourcePort);
    CsrUint32Ser(ptr, len, (u32) primitive->config.customFilter.udpDestPort);
    return(ptr);
}


void* CsrWifiRouterCtrlTrafficConfigReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlTrafficConfigReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlTrafficConfigReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->trafficConfigType, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->config.packetFilter, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->config.customFilter.etherType, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->config.customFilter.ipType, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->config.customFilter.udpSourcePort, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->config.customFilter.udpDestPort, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlWifiOnReqSizeof(void *msg)
{
    CsrWifiRouterCtrlWifiOnReq *primitive = (CsrWifiRouterCtrlWifiOnReq *) msg;
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 10) */
    bufferSize += 2;                     /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 4;                     /* u32 primitive->dataLength */
    bufferSize += primitive->dataLength; /* u8 primitive->data */
    return bufferSize;
}


u8* CsrWifiRouterCtrlWifiOnReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlWifiOnReq *primitive = (CsrWifiRouterCtrlWifiOnReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint32Ser(ptr, len, (u32) primitive->dataLength);
    if (primitive->dataLength)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->data, ((u16) (primitive->dataLength)));
    }
    return(ptr);
}


void* CsrWifiRouterCtrlWifiOnReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlWifiOnReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlWifiOnReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->dataLength, buffer, &offset);
    if (primitive->dataLength)
    {
        primitive->data = kmalloc(primitive->dataLength, GFP_KERNEL);
        CsrMemCpyDes(primitive->data, buffer, &offset, ((u16) (primitive->dataLength)));
    }
    else
    {
        primitive->data = NULL;
    }

    return primitive;
}


void CsrWifiRouterCtrlWifiOnReqSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterCtrlWifiOnReq *primitive = (CsrWifiRouterCtrlWifiOnReq *) voidPrimitivePointer;
    kfree(primitive->data);
    kfree(primitive);
}


size_t CsrWifiRouterCtrlWifiOnResSizeof(void *msg)
{
    CsrWifiRouterCtrlWifiOnRes *primitive = (CsrWifiRouterCtrlWifiOnRes *) msg;
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 30) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* CsrResult primitive->status */
    bufferSize += 2; /* u16 primitive->numInterfaceAddress */
    {
        u16 i1;
        for (i1 = 0; i1 < 2; i1++)
        {
            bufferSize += 6;                                                                            /* u8 primitive->stationMacAddress[i1].a[6] */
        }
    }
    bufferSize += 4;                                                                                    /* u32 primitive->smeVersions.firmwarePatch */
    bufferSize += (primitive->smeVersions.smeBuild ? strlen(primitive->smeVersions.smeBuild) : 0) + 1;  /* char* primitive->smeVersions.smeBuild (0 byte len + 1 for NULL Term) */
    bufferSize += 4;                                                                                    /* u32 primitive->smeVersions.smeHip */
    bufferSize += 1;                                                                                    /* u8 primitive->scheduledInterrupt */
    return bufferSize;
}


u8* CsrWifiRouterCtrlWifiOnResSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlWifiOnRes *primitive = (CsrWifiRouterCtrlWifiOnRes *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    CsrUint16Ser(ptr, len, (u16) primitive->numInterfaceAddress);
    {
        u16 i1;
        for (i1 = 0; i1 < 2; i1++)
        {
            CsrMemCpySer(ptr, len, (const void *) primitive->stationMacAddress[i1].a, ((u16) (6)));
        }
    }
    CsrUint32Ser(ptr, len, (u32) primitive->smeVersions.firmwarePatch);
    CsrCharStringSer(ptr, len, primitive->smeVersions.smeBuild);
    CsrUint32Ser(ptr, len, (u32) primitive->smeVersions.smeHip);
    CsrUint8Ser(ptr, len, (u8) primitive->scheduledInterrupt);
    return(ptr);
}


void* CsrWifiRouterCtrlWifiOnResDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlWifiOnRes *primitive = kmalloc(sizeof(CsrWifiRouterCtrlWifiOnRes), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->numInterfaceAddress, buffer, &offset);
    {
        u16 i1;
        for (i1 = 0; i1 < 2; i1++)
        {
            CsrMemCpyDes(primitive->stationMacAddress[i1].a, buffer, &offset, ((u16) (6)));
        }
    }
    CsrUint32Des((u32 *) &primitive->smeVersions.firmwarePatch, buffer, &offset);
    CsrCharStringDes(&primitive->smeVersions.smeBuild, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->smeVersions.smeHip, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->scheduledInterrupt, buffer, &offset);

    return primitive;
}


void CsrWifiRouterCtrlWifiOnResSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterCtrlWifiOnRes *primitive = (CsrWifiRouterCtrlWifiOnRes *) voidPrimitivePointer;
    kfree(primitive->smeVersions.smeBuild);
    kfree(primitive);
}


size_t CsrWifiRouterCtrlM4TransmitReqSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 7) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    return bufferSize;
}


u8* CsrWifiRouterCtrlM4TransmitReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlM4TransmitReq *primitive = (CsrWifiRouterCtrlM4TransmitReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    return(ptr);
}


void* CsrWifiRouterCtrlM4TransmitReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlM4TransmitReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlM4TransmitReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlModeSetReqSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 16) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 1; /* CsrWifiRouterCtrlMode primitive->mode */
    bufferSize += 6; /* u8 primitive->bssid.a[6] */
    bufferSize += 1; /* u8 primitive->protection */
    bufferSize += 1; /* u8 primitive->intraBssDistEnabled */
    return bufferSize;
}


u8* CsrWifiRouterCtrlModeSetReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlModeSetReq *primitive = (CsrWifiRouterCtrlModeSetReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint8Ser(ptr, len, (u8) primitive->mode);
    CsrMemCpySer(ptr, len, (const void *) primitive->bssid.a, ((u16) (6)));
    CsrUint8Ser(ptr, len, (u8) primitive->protection);
    CsrUint8Ser(ptr, len, (u8) primitive->intraBssDistEnabled);
    return(ptr);
}


void* CsrWifiRouterCtrlModeSetReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlModeSetReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlModeSetReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->mode, buffer, &offset);
    CsrMemCpyDes(primitive->bssid.a, buffer, &offset, ((u16) (6)));
    CsrUint8Des((u8 *) &primitive->protection, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->intraBssDistEnabled, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlPeerAddReqSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 21) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 6; /* u8 primitive->peerMacAddress.a[6] */
    bufferSize += 2; /* u16 primitive->associationId */
    bufferSize += 1; /* u8 primitive->staInfo.wmmOrQosEnabled */
    bufferSize += 2; /* CsrWifiRouterCtrlPowersaveTypeMask primitive->staInfo.powersaveMode */
    bufferSize += 1; /* u8 primitive->staInfo.maxSpLength */
    bufferSize += 2; /* u16 primitive->staInfo.listenIntervalInTus */
    return bufferSize;
}


u8* CsrWifiRouterCtrlPeerAddReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlPeerAddReq *primitive = (CsrWifiRouterCtrlPeerAddReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrMemCpySer(ptr, len, (const void *) primitive->peerMacAddress.a, ((u16) (6)));
    CsrUint16Ser(ptr, len, (u16) primitive->associationId);
    CsrUint8Ser(ptr, len, (u8) primitive->staInfo.wmmOrQosEnabled);
    CsrUint16Ser(ptr, len, (u16) primitive->staInfo.powersaveMode);
    CsrUint8Ser(ptr, len, (u8) primitive->staInfo.maxSpLength);
    CsrUint16Ser(ptr, len, (u16) primitive->staInfo.listenIntervalInTus);
    return(ptr);
}


void* CsrWifiRouterCtrlPeerAddReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlPeerAddReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlPeerAddReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrMemCpyDes(primitive->peerMacAddress.a, buffer, &offset, ((u16) (6)));
    CsrUint16Des((u16 *) &primitive->associationId, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->staInfo.wmmOrQosEnabled, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->staInfo.powersaveMode, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->staInfo.maxSpLength, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->staInfo.listenIntervalInTus, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlPeerDelReqSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 11) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 4; /* CsrWifiRouterCtrlPeerRecordHandle primitive->peerRecordHandle */
    return bufferSize;
}


u8* CsrWifiRouterCtrlPeerDelReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlPeerDelReq *primitive = (CsrWifiRouterCtrlPeerDelReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint32Ser(ptr, len, (u32) primitive->peerRecordHandle);
    return(ptr);
}


void* CsrWifiRouterCtrlPeerDelReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlPeerDelReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlPeerDelReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->peerRecordHandle, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlPeerUpdateReqSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 13) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 4; /* CsrWifiRouterCtrlPeerRecordHandle primitive->peerRecordHandle */
    bufferSize += 2; /* CsrWifiRouterCtrlPowersaveTypeMask primitive->powersaveMode */
    return bufferSize;
}


u8* CsrWifiRouterCtrlPeerUpdateReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlPeerUpdateReq *primitive = (CsrWifiRouterCtrlPeerUpdateReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint32Ser(ptr, len, (u32) primitive->peerRecordHandle);
    CsrUint16Ser(ptr, len, (u16) primitive->powersaveMode);
    return(ptr);
}


void* CsrWifiRouterCtrlPeerUpdateReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlPeerUpdateReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlPeerUpdateReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->peerRecordHandle, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->powersaveMode, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlBlockAckEnableReqSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 21) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 6; /* u8 primitive->macAddress.a[6] */
    bufferSize += 1; /* CsrWifiRouterCtrlTrafficStreamId primitive->trafficStreamID */
    bufferSize += 1; /* CsrWifiRouterCtrlBlockAckRole primitive->role */
    bufferSize += 2; /* u16 primitive->bufferSize */
    bufferSize += 2; /* u16 primitive->timeout */
    bufferSize += 2; /* u16 primitive->ssn */
    return bufferSize;
}


u8* CsrWifiRouterCtrlBlockAckEnableReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlBlockAckEnableReq *primitive = (CsrWifiRouterCtrlBlockAckEnableReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrMemCpySer(ptr, len, (const void *) primitive->macAddress.a, ((u16) (6)));
    CsrUint8Ser(ptr, len, (u8) primitive->trafficStreamID);
    CsrUint8Ser(ptr, len, (u8) primitive->role);
    CsrUint16Ser(ptr, len, (u16) primitive->bufferSize);
    CsrUint16Ser(ptr, len, (u16) primitive->timeout);
    CsrUint16Ser(ptr, len, (u16) primitive->ssn);
    return(ptr);
}


void* CsrWifiRouterCtrlBlockAckEnableReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlBlockAckEnableReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlBlockAckEnableReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrMemCpyDes(primitive->macAddress.a, buffer, &offset, ((u16) (6)));
    CsrUint8Des((u8 *) &primitive->trafficStreamID, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->role, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->bufferSize, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->timeout, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->ssn, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlBlockAckDisableReqSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 15) */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 6; /* u8 primitive->macAddress.a[6] */
    bufferSize += 1; /* CsrWifiRouterCtrlTrafficStreamId primitive->trafficStreamID */
    bufferSize += 1; /* CsrWifiRouterCtrlBlockAckRole primitive->role */
    return bufferSize;
}


u8* CsrWifiRouterCtrlBlockAckDisableReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlBlockAckDisableReq *primitive = (CsrWifiRouterCtrlBlockAckDisableReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrMemCpySer(ptr, len, (const void *) primitive->macAddress.a, ((u16) (6)));
    CsrUint8Ser(ptr, len, (u8) primitive->trafficStreamID);
    CsrUint8Ser(ptr, len, (u8) primitive->role);
    return(ptr);
}


void* CsrWifiRouterCtrlBlockAckDisableReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlBlockAckDisableReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlBlockAckDisableReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrMemCpyDes(primitive->macAddress.a, buffer, &offset, ((u16) (6)));
    CsrUint8Des((u8 *) &primitive->trafficStreamID, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->role, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlWapiRxPktReqSizeof(void *msg)
{
    CsrWifiRouterCtrlWapiRxPktReq *primitive = (CsrWifiRouterCtrlWapiRxPktReq *) msg;
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 11) */
    bufferSize += 2;                       /* u16 primitive->interfaceTag */
    bufferSize += 2;                       /* u16 primitive->signalLength */
    bufferSize += primitive->signalLength; /* u8 primitive->signal */
    bufferSize += 2;                       /* u16 primitive->dataLength */
    bufferSize += primitive->dataLength;   /* u8 primitive->data */
    return bufferSize;
}


u8* CsrWifiRouterCtrlWapiRxPktReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlWapiRxPktReq *primitive = (CsrWifiRouterCtrlWapiRxPktReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->signalLength);
    if (primitive->signalLength)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->signal, ((u16) (primitive->signalLength)));
    }
    CsrUint16Ser(ptr, len, (u16) primitive->dataLength);
    if (primitive->dataLength)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->data, ((u16) (primitive->dataLength)));
    }
    return(ptr);
}


void* CsrWifiRouterCtrlWapiRxPktReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlWapiRxPktReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlWapiRxPktReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->signalLength, buffer, &offset);
    if (primitive->signalLength)
    {
        primitive->signal = kmalloc(primitive->signalLength, GFP_KERNEL);
        CsrMemCpyDes(primitive->signal, buffer, &offset, ((u16) (primitive->signalLength)));
    }
    else
    {
        primitive->signal = NULL;
    }
    CsrUint16Des((u16 *) &primitive->dataLength, buffer, &offset);
    if (primitive->dataLength)
    {
        primitive->data = kmalloc(primitive->dataLength, GFP_KERNEL);
        CsrMemCpyDes(primitive->data, buffer, &offset, ((u16) (primitive->dataLength)));
    }
    else
    {
        primitive->data = NULL;
    }

    return primitive;
}


void CsrWifiRouterCtrlWapiRxPktReqSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterCtrlWapiRxPktReq *primitive = (CsrWifiRouterCtrlWapiRxPktReq *) voidPrimitivePointer;
    kfree(primitive->signal);
    kfree(primitive->data);
    kfree(primitive);
}


size_t CsrWifiRouterCtrlWapiUnicastTxPktReqSizeof(void *msg)
{
    CsrWifiRouterCtrlWapiUnicastTxPktReq *primitive = (CsrWifiRouterCtrlWapiUnicastTxPktReq *) msg;
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 8) */
    bufferSize += 2;                     /* u16 primitive->interfaceTag */
    bufferSize += 2;                     /* u16 primitive->dataLength */
    bufferSize += primitive->dataLength; /* u8 primitive->data */
    return bufferSize;
}


u8* CsrWifiRouterCtrlWapiUnicastTxPktReqSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlWapiUnicastTxPktReq *primitive = (CsrWifiRouterCtrlWapiUnicastTxPktReq *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->dataLength);
    if (primitive->dataLength)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->data, ((u16) (primitive->dataLength)));
    }
    return(ptr);
}


void* CsrWifiRouterCtrlWapiUnicastTxPktReqDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlWapiUnicastTxPktReq *primitive = kmalloc(sizeof(CsrWifiRouterCtrlWapiUnicastTxPktReq), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->dataLength, buffer, &offset);
    if (primitive->dataLength)
    {
        primitive->data = kmalloc(primitive->dataLength, GFP_KERNEL);
        CsrMemCpyDes(primitive->data, buffer, &offset, ((u16) (primitive->dataLength)));
    }
    else
    {
        primitive->data = NULL;
    }

    return primitive;
}


void CsrWifiRouterCtrlWapiUnicastTxPktReqSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterCtrlWapiUnicastTxPktReq *primitive = (CsrWifiRouterCtrlWapiUnicastTxPktReq *) voidPrimitivePointer;
    kfree(primitive->data);
    kfree(primitive);
}


size_t CsrWifiRouterCtrlHipIndSizeof(void *msg)
{
    CsrWifiRouterCtrlHipInd *primitive = (CsrWifiRouterCtrlHipInd *) msg;
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 12) */
    bufferSize += 2;                            /* u16 primitive->mlmeCommandLength */
    bufferSize += primitive->mlmeCommandLength; /* u8 primitive->mlmeCommand */
    bufferSize += 2;                            /* u16 primitive->dataRef1Length */
    bufferSize += primitive->dataRef1Length;    /* u8 primitive->dataRef1 */
    bufferSize += 2;                            /* u16 primitive->dataRef2Length */
    bufferSize += primitive->dataRef2Length;    /* u8 primitive->dataRef2 */
    return bufferSize;
}


u8* CsrWifiRouterCtrlHipIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlHipInd *primitive = (CsrWifiRouterCtrlHipInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->mlmeCommandLength);
    if (primitive->mlmeCommandLength)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->mlmeCommand, ((u16) (primitive->mlmeCommandLength)));
    }
    CsrUint16Ser(ptr, len, (u16) primitive->dataRef1Length);
    if (primitive->dataRef1Length)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->dataRef1, ((u16) (primitive->dataRef1Length)));
    }
    CsrUint16Ser(ptr, len, (u16) primitive->dataRef2Length);
    if (primitive->dataRef2Length)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->dataRef2, ((u16) (primitive->dataRef2Length)));
    }
    return(ptr);
}


void* CsrWifiRouterCtrlHipIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlHipInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlHipInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->mlmeCommandLength, buffer, &offset);
    if (primitive->mlmeCommandLength)
    {
        primitive->mlmeCommand = kmalloc(primitive->mlmeCommandLength, GFP_KERNEL);
        CsrMemCpyDes(primitive->mlmeCommand, buffer, &offset, ((u16) (primitive->mlmeCommandLength)));
    }
    else
    {
        primitive->mlmeCommand = NULL;
    }
    CsrUint16Des((u16 *) &primitive->dataRef1Length, buffer, &offset);
    if (primitive->dataRef1Length)
    {
        primitive->dataRef1 = kmalloc(primitive->dataRef1Length, GFP_KERNEL);
        CsrMemCpyDes(primitive->dataRef1, buffer, &offset, ((u16) (primitive->dataRef1Length)));
    }
    else
    {
        primitive->dataRef1 = NULL;
    }
    CsrUint16Des((u16 *) &primitive->dataRef2Length, buffer, &offset);
    if (primitive->dataRef2Length)
    {
        primitive->dataRef2 = kmalloc(primitive->dataRef2Length, GFP_KERNEL);
        CsrMemCpyDes(primitive->dataRef2, buffer, &offset, ((u16) (primitive->dataRef2Length)));
    }
    else
    {
        primitive->dataRef2 = NULL;
    }

    return primitive;
}


void CsrWifiRouterCtrlHipIndSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterCtrlHipInd *primitive = (CsrWifiRouterCtrlHipInd *) voidPrimitivePointer;
    kfree(primitive->mlmeCommand);
    kfree(primitive->dataRef1);
    kfree(primitive->dataRef2);
    kfree(primitive);
}


size_t CsrWifiRouterCtrlMulticastAddressIndSizeof(void *msg)
{
    CsrWifiRouterCtrlMulticastAddressInd *primitive = (CsrWifiRouterCtrlMulticastAddressInd *) msg;
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 15) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 1; /* CsrWifiRouterCtrlListAction primitive->action */
    bufferSize += 1; /* u8 primitive->setAddressesCount */
    {
        u16 i1;
        for (i1 = 0; i1 < primitive->setAddressesCount; i1++)
        {
            bufferSize += 6; /* u8 primitive->setAddresses[i1].a[6] */
        }
    }
    return bufferSize;
}


u8* CsrWifiRouterCtrlMulticastAddressIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlMulticastAddressInd *primitive = (CsrWifiRouterCtrlMulticastAddressInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint8Ser(ptr, len, (u8) primitive->action);
    CsrUint8Ser(ptr, len, (u8) primitive->setAddressesCount);
    {
        u16 i1;
        for (i1 = 0; i1 < primitive->setAddressesCount; i1++)
        {
            CsrMemCpySer(ptr, len, (const void *) primitive->setAddresses[i1].a, ((u16) (6)));
        }
    }
    return(ptr);
}


void* CsrWifiRouterCtrlMulticastAddressIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlMulticastAddressInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlMulticastAddressInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->action, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->setAddressesCount, buffer, &offset);
    primitive->setAddresses = NULL;
    if (primitive->setAddressesCount)
    {
        primitive->setAddresses = kmalloc(sizeof(CsrWifiMacAddress) * primitive->setAddressesCount, GFP_KERNEL);
    }
    {
        u16 i1;
        for (i1 = 0; i1 < primitive->setAddressesCount; i1++)
        {
            CsrMemCpyDes(primitive->setAddresses[i1].a, buffer, &offset, ((u16) (6)));
        }
    }

    return primitive;
}


void CsrWifiRouterCtrlMulticastAddressIndSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterCtrlMulticastAddressInd *primitive = (CsrWifiRouterCtrlMulticastAddressInd *) voidPrimitivePointer;
    kfree(primitive->setAddresses);
    kfree(primitive);
}


size_t CsrWifiRouterCtrlPortConfigureCfmSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 15) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrResult primitive->status */
    bufferSize += 6; /* u8 primitive->macAddress.a[6] */
    return bufferSize;
}


u8* CsrWifiRouterCtrlPortConfigureCfmSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlPortConfigureCfm *primitive = (CsrWifiRouterCtrlPortConfigureCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    CsrMemCpySer(ptr, len, (const void *) primitive->macAddress.a, ((u16) (6)));
    return(ptr);
}


void* CsrWifiRouterCtrlPortConfigureCfmDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlPortConfigureCfm *primitive = kmalloc(sizeof(CsrWifiRouterCtrlPortConfigureCfm), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);
    CsrMemCpyDes(primitive->macAddress.a, buffer, &offset, ((u16) (6)));

    return primitive;
}


size_t CsrWifiRouterCtrlSuspendIndSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 7) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 1; /* u8 primitive->hardSuspend */
    bufferSize += 1; /* u8 primitive->d3Suspend */
    return bufferSize;
}


u8* CsrWifiRouterCtrlSuspendIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlSuspendInd *primitive = (CsrWifiRouterCtrlSuspendInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint8Ser(ptr, len, (u8) primitive->hardSuspend);
    CsrUint8Ser(ptr, len, (u8) primitive->d3Suspend);
    return(ptr);
}


void* CsrWifiRouterCtrlSuspendIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlSuspendInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlSuspendInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->hardSuspend, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->d3Suspend, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlTclasAddCfmSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 9) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiRouterCtrlTclasAddCfmSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlTclasAddCfm *primitive = (CsrWifiRouterCtrlTclasAddCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiRouterCtrlTclasAddCfmDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlTclasAddCfm *primitive = kmalloc(sizeof(CsrWifiRouterCtrlTclasAddCfm), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlRawSdioDeinitialiseCfmSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 7) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* CsrResult primitive->result */
    return bufferSize;
}


u8* CsrWifiRouterCtrlRawSdioDeinitialiseCfmSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlRawSdioDeinitialiseCfm *primitive = (CsrWifiRouterCtrlRawSdioDeinitialiseCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->result);
    return(ptr);
}


void* CsrWifiRouterCtrlRawSdioDeinitialiseCfmDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlRawSdioDeinitialiseCfm *primitive = kmalloc(sizeof(CsrWifiRouterCtrlRawSdioDeinitialiseCfm), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->result, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlRawSdioInitialiseCfmSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 39) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* CsrResult primitive->result */
    bufferSize += 4; /* CsrWifiRouterCtrlRawSdioByteRead primitive->byteRead */
    bufferSize += 4; /* CsrWifiRouterCtrlRawSdioByteWrite primitive->byteWrite */
    bufferSize += 4; /* CsrWifiRouterCtrlRawSdioFirmwareDownload primitive->firmwareDownload */
    bufferSize += 4; /* CsrWifiRouterCtrlRawSdioReset primitive->reset */
    bufferSize += 4; /* CsrWifiRouterCtrlRawSdioCoreDumpPrepare primitive->coreDumpPrepare */
    bufferSize += 4; /* CsrWifiRouterCtrlRawSdioByteBlockRead primitive->byteBlockRead */
    bufferSize += 4; /* CsrWifiRouterCtrlRawSdioGpRead16 primitive->gpRead16 */
    bufferSize += 4; /* CsrWifiRouterCtrlRawSdioGpWrite16 primitive->gpWrite16 */
    return bufferSize;
}


u8* CsrWifiRouterCtrlRawSdioInitialiseCfmSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlRawSdioInitialiseCfm *primitive = (CsrWifiRouterCtrlRawSdioInitialiseCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->result);
    CsrUint32Ser(ptr, len, 0); /* Special for Function Pointers... primitive->byteRead */
    CsrUint32Ser(ptr, len, 0); /* Special for Function Pointers... primitive->byteWrite */
    CsrUint32Ser(ptr, len, 0); /* Special for Function Pointers... primitive->firmwareDownload */
    CsrUint32Ser(ptr, len, 0); /* Special for Function Pointers... primitive->reset */
    CsrUint32Ser(ptr, len, 0); /* Special for Function Pointers... primitive->coreDumpPrepare */
    CsrUint32Ser(ptr, len, 0); /* Special for Function Pointers... primitive->byteBlockRead */
    CsrUint32Ser(ptr, len, 0); /* Special for Function Pointers... primitive->gpRead16 */
    CsrUint32Ser(ptr, len, 0); /* Special for Function Pointers... primitive->gpWrite16 */
    return(ptr);
}


void* CsrWifiRouterCtrlRawSdioInitialiseCfmDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlRawSdioInitialiseCfm *primitive = kmalloc(sizeof(CsrWifiRouterCtrlRawSdioInitialiseCfm), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->result, buffer, &offset);
    primitive->byteRead = NULL;         /* Special for Function Pointers... */
    offset += 4;
    primitive->byteWrite = NULL;        /* Special for Function Pointers... */
    offset += 4;
    primitive->firmwareDownload = NULL; /* Special for Function Pointers... */
    offset += 4;
    primitive->reset = NULL;            /* Special for Function Pointers... */
    offset += 4;
    primitive->coreDumpPrepare = NULL;  /* Special for Function Pointers... */
    offset += 4;
    primitive->byteBlockRead = NULL;    /* Special for Function Pointers... */
    offset += 4;
    primitive->gpRead16 = NULL;         /* Special for Function Pointers... */
    offset += 4;
    primitive->gpWrite16 = NULL;        /* Special for Function Pointers... */
    offset += 4;

    return primitive;
}


size_t CsrWifiRouterCtrlTclasDelCfmSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 9) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiRouterCtrlTclasDelCfmSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlTclasDelCfm *primitive = (CsrWifiRouterCtrlTclasDelCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiRouterCtrlTclasDelCfmDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlTclasDelCfm *primitive = kmalloc(sizeof(CsrWifiRouterCtrlTclasDelCfm), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlTrafficProtocolIndSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 17) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrWifiRouterCtrlTrafficPacketType primitive->packetType */
    bufferSize += 2; /* CsrWifiRouterCtrlProtocolDirection primitive->direction */
    bufferSize += 6; /* u8 primitive->srcAddress.a[6] */
    return bufferSize;
}


u8* CsrWifiRouterCtrlTrafficProtocolIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlTrafficProtocolInd *primitive = (CsrWifiRouterCtrlTrafficProtocolInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->packetType);
    CsrUint16Ser(ptr, len, (u16) primitive->direction);
    CsrMemCpySer(ptr, len, (const void *) primitive->srcAddress.a, ((u16) (6)));
    return(ptr);
}


void* CsrWifiRouterCtrlTrafficProtocolIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlTrafficProtocolInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlTrafficProtocolInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->packetType, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->direction, buffer, &offset);
    CsrMemCpyDes(primitive->srcAddress.a, buffer, &offset, ((u16) (6)));

    return primitive;
}


size_t CsrWifiRouterCtrlTrafficSampleIndSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 38) */
    bufferSize += 2;  /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2;  /* u16 primitive->interfaceTag */
    bufferSize += 4;  /* u32 primitive->stats.rxMeanRate */
    bufferSize += 4;  /* u32 primitive->stats.rxFramesNum */
    bufferSize += 4;  /* u32 primitive->stats.txFramesNum */
    bufferSize += 4;  /* u32 primitive->stats.rxBytesCount */
    bufferSize += 4;  /* u32 primitive->stats.txBytesCount */
    bufferSize += 11; /* u8 primitive->stats.intervals[11] */
    return bufferSize;
}


u8* CsrWifiRouterCtrlTrafficSampleIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlTrafficSampleInd *primitive = (CsrWifiRouterCtrlTrafficSampleInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint32Ser(ptr, len, (u32) primitive->stats.rxMeanRate);
    CsrUint32Ser(ptr, len, (u32) primitive->stats.rxFramesNum);
    CsrUint32Ser(ptr, len, (u32) primitive->stats.txFramesNum);
    CsrUint32Ser(ptr, len, (u32) primitive->stats.rxBytesCount);
    CsrUint32Ser(ptr, len, (u32) primitive->stats.txBytesCount);
    CsrMemCpySer(ptr, len, (const void *) primitive->stats.intervals, ((u16) (11)));
    return(ptr);
}


void* CsrWifiRouterCtrlTrafficSampleIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlTrafficSampleInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlTrafficSampleInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->stats.rxMeanRate, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->stats.rxFramesNum, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->stats.txFramesNum, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->stats.rxBytesCount, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->stats.txBytesCount, buffer, &offset);
    CsrMemCpyDes(primitive->stats.intervals, buffer, &offset, ((u16) (11)));

    return primitive;
}


size_t CsrWifiRouterCtrlWifiOnIndSizeof(void *msg)
{
    CsrWifiRouterCtrlWifiOnInd *primitive = (CsrWifiRouterCtrlWifiOnInd *) msg;
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 27) */
    bufferSize += 2;                                                                                    /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2;                                                                                    /* CsrResult primitive->status */
    bufferSize += 4;                                                                                    /* u32 primitive->versions.chipId */
    bufferSize += 4;                                                                                    /* u32 primitive->versions.chipVersion */
    bufferSize += 4;                                                                                    /* u32 primitive->versions.firmwareBuild */
    bufferSize += 4;                                                                                    /* u32 primitive->versions.firmwareHip */
    bufferSize += (primitive->versions.routerBuild ? strlen(primitive->versions.routerBuild) : 0) + 1;  /* char* primitive->versions.routerBuild (0 byte len + 1 for NULL Term) */
    bufferSize += 4;                                                                                    /* u32 primitive->versions.routerHip */
    return bufferSize;
}


u8* CsrWifiRouterCtrlWifiOnIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlWifiOnInd *primitive = (CsrWifiRouterCtrlWifiOnInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    CsrUint32Ser(ptr, len, (u32) primitive->versions.chipId);
    CsrUint32Ser(ptr, len, (u32) primitive->versions.chipVersion);
    CsrUint32Ser(ptr, len, (u32) primitive->versions.firmwareBuild);
    CsrUint32Ser(ptr, len, (u32) primitive->versions.firmwareHip);
    CsrCharStringSer(ptr, len, primitive->versions.routerBuild);
    CsrUint32Ser(ptr, len, (u32) primitive->versions.routerHip);
    return(ptr);
}


void* CsrWifiRouterCtrlWifiOnIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlWifiOnInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlWifiOnInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->versions.chipId, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->versions.chipVersion, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->versions.firmwareBuild, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->versions.firmwareHip, buffer, &offset);
    CsrCharStringDes(&primitive->versions.routerBuild, buffer, &offset);
    CsrUint32Des((u32 *) &primitive->versions.routerHip, buffer, &offset);

    return primitive;
}


void CsrWifiRouterCtrlWifiOnIndSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterCtrlWifiOnInd *primitive = (CsrWifiRouterCtrlWifiOnInd *) voidPrimitivePointer;
    kfree(primitive->versions.routerBuild);
    kfree(primitive);
}


size_t CsrWifiRouterCtrlWifiOnCfmSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 7) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiRouterCtrlWifiOnCfmSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlWifiOnCfm *primitive = (CsrWifiRouterCtrlWifiOnCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiRouterCtrlWifiOnCfmDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlWifiOnCfm *primitive = kmalloc(sizeof(CsrWifiRouterCtrlWifiOnCfm), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlM4ReadyToSendIndSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 13) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 6; /* u8 primitive->peerMacAddress.a[6] */
    return bufferSize;
}


u8* CsrWifiRouterCtrlM4ReadyToSendIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlM4ReadyToSendInd *primitive = (CsrWifiRouterCtrlM4ReadyToSendInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrMemCpySer(ptr, len, (const void *) primitive->peerMacAddress.a, ((u16) (6)));
    return(ptr);
}


void* CsrWifiRouterCtrlM4ReadyToSendIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlM4ReadyToSendInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlM4ReadyToSendInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrMemCpyDes(primitive->peerMacAddress.a, buffer, &offset, ((u16) (6)));

    return primitive;
}


size_t CsrWifiRouterCtrlM4TransmittedIndSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 15) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 6; /* u8 primitive->peerMacAddress.a[6] */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiRouterCtrlM4TransmittedIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlM4TransmittedInd *primitive = (CsrWifiRouterCtrlM4TransmittedInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrMemCpySer(ptr, len, (const void *) primitive->peerMacAddress.a, ((u16) (6)));
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiRouterCtrlM4TransmittedIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlM4TransmittedInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlM4TransmittedInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrMemCpyDes(primitive->peerMacAddress.a, buffer, &offset, ((u16) (6)));
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlMicFailureIndSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 14) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 6; /* u8 primitive->peerMacAddress.a[6] */
    bufferSize += 1; /* u8 primitive->unicastPdu */
    return bufferSize;
}


u8* CsrWifiRouterCtrlMicFailureIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlMicFailureInd *primitive = (CsrWifiRouterCtrlMicFailureInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrMemCpySer(ptr, len, (const void *) primitive->peerMacAddress.a, ((u16) (6)));
    CsrUint8Ser(ptr, len, (u8) primitive->unicastPdu);
    return(ptr);
}


void* CsrWifiRouterCtrlMicFailureIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlMicFailureInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlMicFailureInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrMemCpyDes(primitive->peerMacAddress.a, buffer, &offset, ((u16) (6)));
    CsrUint8Des((u8 *) &primitive->unicastPdu, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlConnectedIndSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 14) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 6; /* u8 primitive->peerMacAddress.a[6] */
    bufferSize += 1; /* CsrWifiRouterCtrlPeerStatus primitive->peerStatus */
    return bufferSize;
}


u8* CsrWifiRouterCtrlConnectedIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlConnectedInd *primitive = (CsrWifiRouterCtrlConnectedInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrMemCpySer(ptr, len, (const void *) primitive->peerMacAddress.a, ((u16) (6)));
    CsrUint8Ser(ptr, len, (u8) primitive->peerStatus);
    return(ptr);
}


void* CsrWifiRouterCtrlConnectedIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlConnectedInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlConnectedInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrMemCpyDes(primitive->peerMacAddress.a, buffer, &offset, ((u16) (6)));
    CsrUint8Des((u8 *) &primitive->peerStatus, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlPeerAddCfmSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 19) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 6; /* u8 primitive->peerMacAddress.a[6] */
    bufferSize += 4; /* CsrWifiRouterCtrlPeerRecordHandle primitive->peerRecordHandle */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiRouterCtrlPeerAddCfmSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlPeerAddCfm *primitive = (CsrWifiRouterCtrlPeerAddCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrMemCpySer(ptr, len, (const void *) primitive->peerMacAddress.a, ((u16) (6)));
    CsrUint32Ser(ptr, len, (u32) primitive->peerRecordHandle);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiRouterCtrlPeerAddCfmDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlPeerAddCfm *primitive = kmalloc(sizeof(CsrWifiRouterCtrlPeerAddCfm), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrMemCpyDes(primitive->peerMacAddress.a, buffer, &offset, ((u16) (6)));
    CsrUint32Des((u32 *) &primitive->peerRecordHandle, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlPeerDelCfmSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 9) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiRouterCtrlPeerDelCfmSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlPeerDelCfm *primitive = (CsrWifiRouterCtrlPeerDelCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiRouterCtrlPeerDelCfmDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlPeerDelCfm *primitive = kmalloc(sizeof(CsrWifiRouterCtrlPeerDelCfm), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlUnexpectedFrameIndSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 13) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 6; /* u8 primitive->peerMacAddress.a[6] */
    return bufferSize;
}


u8* CsrWifiRouterCtrlUnexpectedFrameIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlUnexpectedFrameInd *primitive = (CsrWifiRouterCtrlUnexpectedFrameInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrMemCpySer(ptr, len, (const void *) primitive->peerMacAddress.a, ((u16) (6)));
    return(ptr);
}


void* CsrWifiRouterCtrlUnexpectedFrameIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlUnexpectedFrameInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlUnexpectedFrameInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrMemCpyDes(primitive->peerMacAddress.a, buffer, &offset, ((u16) (6)));

    return primitive;
}


size_t CsrWifiRouterCtrlPeerUpdateCfmSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 9) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiRouterCtrlPeerUpdateCfmSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlPeerUpdateCfm *primitive = (CsrWifiRouterCtrlPeerUpdateCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiRouterCtrlPeerUpdateCfmDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlPeerUpdateCfm *primitive = kmalloc(sizeof(CsrWifiRouterCtrlPeerUpdateCfm), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlCapabilitiesCfmSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 9) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->commandQueueSize */
    bufferSize += 2; /* u16 primitive->trafficQueueSize */
    return bufferSize;
}


u8* CsrWifiRouterCtrlCapabilitiesCfmSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlCapabilitiesCfm *primitive = (CsrWifiRouterCtrlCapabilitiesCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->commandQueueSize);
    CsrUint16Ser(ptr, len, (u16) primitive->trafficQueueSize);
    return(ptr);
}


void* CsrWifiRouterCtrlCapabilitiesCfmDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlCapabilitiesCfm *primitive = kmalloc(sizeof(CsrWifiRouterCtrlCapabilitiesCfm), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->commandQueueSize, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->trafficQueueSize, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlBlockAckEnableCfmSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 9) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiRouterCtrlBlockAckEnableCfmSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlBlockAckEnableCfm *primitive = (CsrWifiRouterCtrlBlockAckEnableCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiRouterCtrlBlockAckEnableCfmDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlBlockAckEnableCfm *primitive = kmalloc(sizeof(CsrWifiRouterCtrlBlockAckEnableCfm), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlBlockAckDisableCfmSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 9) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiRouterCtrlBlockAckDisableCfmSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlBlockAckDisableCfm *primitive = (CsrWifiRouterCtrlBlockAckDisableCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiRouterCtrlBlockAckDisableCfmDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlBlockAckDisableCfm *primitive = kmalloc(sizeof(CsrWifiRouterCtrlBlockAckDisableCfm), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlBlockAckErrorIndSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 16) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 1; /* CsrWifiRouterCtrlTrafficStreamId primitive->trafficStreamID */
    bufferSize += 6; /* u8 primitive->peerMacAddress.a[6] */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiRouterCtrlBlockAckErrorIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlBlockAckErrorInd *primitive = (CsrWifiRouterCtrlBlockAckErrorInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint8Ser(ptr, len, (u8) primitive->trafficStreamID);
    CsrMemCpySer(ptr, len, (const void *) primitive->peerMacAddress.a, ((u16) (6)));
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiRouterCtrlBlockAckErrorIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlBlockAckErrorInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlBlockAckErrorInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->trafficStreamID, buffer, &offset);
    CsrMemCpyDes(primitive->peerMacAddress.a, buffer, &offset, ((u16) (6)));
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlStaInactiveIndSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 13) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 6; /* u8 primitive->staAddress.a[6] */
    return bufferSize;
}


u8* CsrWifiRouterCtrlStaInactiveIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlStaInactiveInd *primitive = (CsrWifiRouterCtrlStaInactiveInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrMemCpySer(ptr, len, (const void *) primitive->staAddress.a, ((u16) (6)));
    return(ptr);
}


void* CsrWifiRouterCtrlStaInactiveIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlStaInactiveInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlStaInactiveInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrMemCpyDes(primitive->staAddress.a, buffer, &offset, ((u16) (6)));

    return primitive;
}


size_t CsrWifiRouterCtrlWapiRxMicCheckIndSizeof(void *msg)
{
    CsrWifiRouterCtrlWapiRxMicCheckInd *primitive = (CsrWifiRouterCtrlWapiRxMicCheckInd *) msg;
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 13) */
    bufferSize += 2;                       /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2;                       /* u16 primitive->interfaceTag */
    bufferSize += 2;                       /* u16 primitive->signalLength */
    bufferSize += primitive->signalLength; /* u8 primitive->signal */
    bufferSize += 2;                       /* u16 primitive->dataLength */
    bufferSize += primitive->dataLength;   /* u8 primitive->data */
    return bufferSize;
}


u8* CsrWifiRouterCtrlWapiRxMicCheckIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlWapiRxMicCheckInd *primitive = (CsrWifiRouterCtrlWapiRxMicCheckInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->signalLength);
    if (primitive->signalLength)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->signal, ((u16) (primitive->signalLength)));
    }
    CsrUint16Ser(ptr, len, (u16) primitive->dataLength);
    if (primitive->dataLength)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->data, ((u16) (primitive->dataLength)));
    }
    return(ptr);
}


void* CsrWifiRouterCtrlWapiRxMicCheckIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlWapiRxMicCheckInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlWapiRxMicCheckInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->signalLength, buffer, &offset);
    if (primitive->signalLength)
    {
        primitive->signal = kmalloc(primitive->signalLength, GFP_KERNEL);
        CsrMemCpyDes(primitive->signal, buffer, &offset, ((u16) (primitive->signalLength)));
    }
    else
    {
        primitive->signal = NULL;
    }
    CsrUint16Des((u16 *) &primitive->dataLength, buffer, &offset);
    if (primitive->dataLength)
    {
        primitive->data = kmalloc(primitive->dataLength, GFP_KERNEL);
        CsrMemCpyDes(primitive->data, buffer, &offset, ((u16) (primitive->dataLength)));
    }
    else
    {
        primitive->data = NULL;
    }

    return primitive;
}


void CsrWifiRouterCtrlWapiRxMicCheckIndSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterCtrlWapiRxMicCheckInd *primitive = (CsrWifiRouterCtrlWapiRxMicCheckInd *) voidPrimitivePointer;
    kfree(primitive->signal);
    kfree(primitive->data);
    kfree(primitive);
}


size_t CsrWifiRouterCtrlModeSetCfmSizeof(void *msg)
{
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 10) */
    bufferSize += 2; /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2; /* u16 primitive->interfaceTag */
    bufferSize += 1; /* CsrWifiRouterCtrlMode primitive->mode */
    bufferSize += 2; /* CsrResult primitive->status */
    return bufferSize;
}


u8* CsrWifiRouterCtrlModeSetCfmSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlModeSetCfm *primitive = (CsrWifiRouterCtrlModeSetCfm *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint8Ser(ptr, len, (u8) primitive->mode);
    CsrUint16Ser(ptr, len, (u16) primitive->status);
    return(ptr);
}


void* CsrWifiRouterCtrlModeSetCfmDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlModeSetCfm *primitive = kmalloc(sizeof(CsrWifiRouterCtrlModeSetCfm), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint8Des((u8 *) &primitive->mode, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->status, buffer, &offset);

    return primitive;
}


size_t CsrWifiRouterCtrlWapiUnicastTxEncryptIndSizeof(void *msg)
{
    CsrWifiRouterCtrlWapiUnicastTxEncryptInd *primitive = (CsrWifiRouterCtrlWapiUnicastTxEncryptInd *) msg;
    size_t bufferSize = 2;

    /* Calculate the Size of the Serialised Data. Could be more efficient (Try 10) */
    bufferSize += 2;                     /* CsrWifiRouterCtrlRequestorInfo primitive->clientData */
    bufferSize += 2;                     /* u16 primitive->interfaceTag */
    bufferSize += 2;                     /* u16 primitive->dataLength */
    bufferSize += primitive->dataLength; /* u8 primitive->data */
    return bufferSize;
}


u8* CsrWifiRouterCtrlWapiUnicastTxEncryptIndSer(u8 *ptr, size_t *len, void *msg)
{
    CsrWifiRouterCtrlWapiUnicastTxEncryptInd *primitive = (CsrWifiRouterCtrlWapiUnicastTxEncryptInd *)msg;
    *len = 0;
    CsrUint16Ser(ptr, len, primitive->common.type);
    CsrUint16Ser(ptr, len, (u16) primitive->clientData);
    CsrUint16Ser(ptr, len, (u16) primitive->interfaceTag);
    CsrUint16Ser(ptr, len, (u16) primitive->dataLength);
    if (primitive->dataLength)
    {
        CsrMemCpySer(ptr, len, (const void *) primitive->data, ((u16) (primitive->dataLength)));
    }
    return(ptr);
}


void* CsrWifiRouterCtrlWapiUnicastTxEncryptIndDes(u8 *buffer, size_t length)
{
    CsrWifiRouterCtrlWapiUnicastTxEncryptInd *primitive = kmalloc(sizeof(CsrWifiRouterCtrlWapiUnicastTxEncryptInd), GFP_KERNEL);
    size_t offset;
    offset = 0;

    CsrUint16Des(&primitive->common.type, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->clientData, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->interfaceTag, buffer, &offset);
    CsrUint16Des((u16 *) &primitive->dataLength, buffer, &offset);
    if (primitive->dataLength)
    {
        primitive->data = kmalloc(primitive->dataLength, GFP_KERNEL);
        CsrMemCpyDes(primitive->data, buffer, &offset, ((u16) (primitive->dataLength)));
    }
    else
    {
        primitive->data = NULL;
    }

    return primitive;
}


void CsrWifiRouterCtrlWapiUnicastTxEncryptIndSerFree(void *voidPrimitivePointer)
{
    CsrWifiRouterCtrlWapiUnicastTxEncryptInd *primitive = (CsrWifiRouterCtrlWapiUnicastTxEncryptInd *) voidPrimitivePointer;
    kfree(primitive->data);
    kfree(primitive);
}


