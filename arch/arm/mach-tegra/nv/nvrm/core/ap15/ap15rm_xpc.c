/*
 * Copyright (c) 2010 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/** 
 * @file
 * @brief <b>nVIDIA Driver Development Kit: 
 *           Cross Proc Communication driver </b>
 *
 * @b Description: Implements the interface to the NvDdk XPC.
 * 
 */

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/io.h>

#include "nvrm_xpc.h"
#include "nvrm_memmgr.h"
#include "ap15rm_xpc_hw_private.h"
#include "nvrm_hardware_access.h"
#include "nvassert.h"
#include "ap15/ararb_sema.h"
#include "ap15/arictlr_arbgnt.h"
#include "nvrm_avp_shrd_interrupt.h"

// Minimum sdram offset required so that avp can access the address which is 
// passed.
// AVP can not access the 0x0000:0000 to 0x0000:0040
enum { MIN_SDRAM_OFFSET = 0x100};


//There are only 32 arb semaphores
#define MAX_ARB_NUM 32

#define ARBSEMA_REG_READ(pArbSemaVirtAdd, reg) \
        NV_READ32(pArbSemaVirtAdd + (ARB_SEMA_##reg##_0))

#define ARBSEMA_REG_WRITE(pArbSemaVirtAdd, reg, data) \
        NV_WRITE32(pArbSemaVirtAdd + (ARB_SEMA_##reg##_0), (data));

#define ARBGNT_REG_READ(pArbGntVirtAdd, reg) \
        NV_READ32(pArbGntVirtAdd + (ARBGNT_##reg##_0))

#define ARBGNT_REG_WRITE(pArbGntVirtAdd, reg, data) \
        NV_WRITE32(pArbGntVirtAdd + (ARBGNT_##reg##_0), (data));

static int s_arbInterruptHandle = -1;

// Combines the Processor Xpc system details. This contains the details of the
// receive/send message queue and messaging system.
typedef struct NvRmPrivXpcMessageRec
{
    NvRmDeviceHandle hDevice;

    // Hw mail box register.
    CpuAvpHwMailBoxReg HwMailBoxReg;

} NvRmPrivXpcMessage;

typedef struct NvRmPrivXpcArbSemaRec
{
    NvRmDeviceHandle hDevice;
    NvU8 *pArbSemaVirtAddr;
    NvU8 *pArbGntVirtAddr;
    NvOsSemaphoreHandle semaphore[MAX_ARB_NUM];
    NvOsMutexHandle mutex[MAX_ARB_NUM];
    NvOsIntrMutexHandle hIntrMutex;

} NvRmPrivXpcArbSema;

static NvRmPrivXpcArbSema s_ArbSema;

//Forward declarations
static NvError InitArbSemaSystem(NvRmDeviceHandle hDevice);
static void ArbSemaIsr(void *args);
NvU32 GetArbIdFromRmModuleId(NvRmModuleID modId);
/**
 * Initialize the cpu avp hw mail box address and map the hw register address 
 * to virtual address.
 * Thread Safety: Caller responsibility
 */
static NvError 
InitializeCpuAvpHwMailBoxRegister(NvRmPrivXpcMessageHandle hXpcMessage)
{
    NvRmPhysAddr ResourceSemaPhysAddr;

    // Get base address of the hw mail box register. This register is in the set
    // of resource semaphore module Id.
    ResourceSemaPhysAddr = TEGRA_RES_SEMA_BASE;
    hXpcMessage->HwMailBoxReg.BankSize = TEGRA_RES_SEMA_SIZE;

    // Map the base address to the virtual address.
    hXpcMessage->HwMailBoxReg.pHwMailBoxRegBaseVirtAddr =
      IO_ADDRESS(ResourceSemaPhysAddr);

    NvRmPrivXpcHwResetOutbox(&hXpcMessage->HwMailBoxReg);

    return NvSuccess;
}

/**
 * DeInitialize the cpu avp hw mail box address and unmap the hw register address 
 * virtual address.
 * Thread Safety: Caller responsibility
 */
static void DeInitializeCpuAvpHwMailBoxRegister(NvRmPrivXpcMessageHandle hXpcMessage)
{
    hXpcMessage->HwMailBoxReg.pHwMailBoxRegBaseVirtAddr = NULL;        
}

/**
 * Create the cpu-avp messaging system.
 * This function will call other helper function to create the messaging technique 
 * used for cpu-avp communication.
 * Thread Safety: Caller responsibility
 */
static NvError 
CreateCpuAvpMessagingSystem(NvRmPrivXpcMessageHandle hXpcMessage)
{
    NvError Error = NvSuccess;

    Error = InitializeCpuAvpHwMailBoxRegister(hXpcMessage);

#if NV_IS_AVP
    hXpcMessage->HwMailBoxReg.IsCpu = NV_FALSE;
#else            
    hXpcMessage->HwMailBoxReg.IsCpu = NV_TRUE;
#endif            
    
    // If error found then destroy all the allocation and initialization,
    if (Error)
        DeInitializeCpuAvpHwMailBoxRegister(hXpcMessage);

    return Error;
}


/**
 * Destroy the cpu-avp messaging system.
 * This function destroy all the allocation/initialization done for creating
 * the cpu-avp messaging system.
 * Thread Safety: Caller responsibility
 */
static void DestroyCpuAvpMessagingSystem(NvRmPrivXpcMessageHandle hXpcMessage)
{
    // Destroy the cpu-avp hw mail box registers. 
    DeInitializeCpuAvpHwMailBoxRegister(hXpcMessage);
    hXpcMessage->HwMailBoxReg.pHwMailBoxRegBaseVirtAddr = NULL;
    hXpcMessage->HwMailBoxReg.BankSize = 0;
}


NvError 
NvRmPrivXpcCreate(
    NvRmDeviceHandle hDevice,
    NvRmPrivXpcMessageHandle *phXpcMessage)
{
    NvError Error = NvSuccess;
    NvRmPrivXpcMessageHandle hNewXpcMsgHandle = NULL;

    *phXpcMessage = NULL;

    // Allocates the memory for the xpc message handle.
    hNewXpcMsgHandle = NvOsAlloc(sizeof(*hNewXpcMsgHandle));
    if (!hNewXpcMsgHandle)
    {
        return NvError_InsufficientMemory;
    }

    // Initialize all the members of the xpc message handle.
    hNewXpcMsgHandle->hDevice = hDevice;
    hNewXpcMsgHandle->HwMailBoxReg.pHwMailBoxRegBaseVirtAddr = NULL;
    hNewXpcMsgHandle->HwMailBoxReg.BankSize = 0;

    // Create the messaging system between the processors.
    Error = CreateCpuAvpMessagingSystem(hNewXpcMsgHandle);

    // if error the destroy all allocations done here.    
    if (Error)
    {
        NvOsFree(hNewXpcMsgHandle);
        hNewXpcMsgHandle = NULL;
    }

#if NV_IS_AVP
    Error = InitArbSemaSystem(hDevice);
    if (Error)
    {
        NvOsFree(hNewXpcMsgHandle);
        hNewXpcMsgHandle = NULL;
    }
#endif

    // Copy the new xpc message handle into the passed parameter.
    *phXpcMessage = hNewXpcMsgHandle;
    return Error;
}


/**
 * Destroy the Rm Xpc message handle.
 * Thread Safety: It is provided inside the function.
 */
void NvRmPrivXpcDestroy(NvRmPrivXpcMessageHandle hXpcMessage)
{
    // If not a null pointer then destroy.
    if (hXpcMessage)
    {
        // Destroy the messaging system between processor.
        DestroyCpuAvpMessagingSystem(hXpcMessage);

        // Free the allocated memory for the xpc message handle.
        NvOsFree(hXpcMessage);
    }
}


// Set the outbound mailbox with the given data.  We might have to spin until
// it's safe to send the message.
NvError
NvRmPrivXpcSendMessage(NvRmPrivXpcMessageHandle hXpcMessage, NvU32 data)
{
    NvRmPrivXpcHwSendMessageToTarget(&hXpcMessage->HwMailBoxReg, data);
    return NvSuccess;
}


// Get the value currently in the inbox register.  This read clears the incoming
// interrupt.
NvU32
NvRmPrivXpcGetMessage(NvRmPrivXpcMessageHandle hXpcMessage)
{
    NvU32 data;
    NvRmPrivXpcHwReceiveMessageFromTarget(&hXpcMessage->HwMailBoxReg, &data);
    return data;
}

NvError NvRmXpcInitArbSemaSystem(NvRmDeviceHandle hDevice)
{
#if NV_IS_AVP
    return NvSuccess;
#else
    return InitArbSemaSystem(hDevice);
#endif
}

static irqreturn_t arbgnt_isr(int irq, void *data)
{
    ArbSemaIsr(data);
    return IRQ_HANDLED;
}

static NvError InitArbSemaSystem(NvRmDeviceHandle hDevice)
{
    NvOsInterruptHandler ArbSemaHandler;
    NvRmPhysAddr ArbSemaBase, ArbGntBase;
    NvU32        ArbSemaSize, ArbGntSize;
    NvU32 irq;
    NvError e;
    NvU32 i = 0;
    int ret;

    /* FIXME:  is this the right interrupt? */
    irq = INT_GNT_0;

    ArbSemaHandler = ArbSemaIsr;
    set_irq_flags(irq, IRQF_VALID | IRQF_NOAUTOEN);
    ret = request_irq(irq, arbgnt_isr, 0, "nvrm_arbgnt", hDevice);
    if (ret < 0) {
      printk("%s request_irq failed %d\n", __func__, ret);
      return NvError_AccessDenied;
    }
    s_arbInterruptHandle = irq;

    ArbSemaBase = TEGRA_ARB_SEMA_BASE;
    ArbSemaSize = TEGRA_ARB_SEMA_SIZE;
    ArbGntBase = TEGRA_ARBGNT_ICTLR_BASE;
    ArbGntSize = TEGRA_ARBGNT_ICTLR_SIZE;

    s_ArbSema.pArbSemaVirtAddr = IO_ADDRESS(ArbSemaBase);
    s_ArbSema.pArbGntVirtAddr = IO_ADDRESS(ArbGntBase);

    //Initialize all the semaphores and mutexes
    for (i=0;i<MAX_ARB_NUM;i++)
    {
        NV_CHECK_ERROR_CLEANUP(
            NvOsSemaphoreCreate(&s_ArbSema.semaphore[i], 0)
        );

        NV_CHECK_ERROR_CLEANUP(
            NvOsMutexCreate(&s_ArbSema.mutex[i])
        );
    }

    NV_CHECK_ERROR_CLEANUP(
        NvOsIntrMutexCreate(&s_ArbSema.hIntrMutex)
    );

    enable_irq(irq);

fail:

    return e;
}


static void ArbSemaIsr(void *args)
{
    NvU32 int_mask, proc_int_enable, arb_gnt, i = 0;

    NvOsIntrMutexLock(s_ArbSema.hIntrMutex);
    //Check which arb semaphores have been granted to this processor
    arb_gnt = ARBSEMA_REG_READ(s_ArbSema.pArbSemaVirtAddr, SMP_GNT_ST);

    //Figure out which arb semaphores were signalled and then disable them.
#if NV_IS_AVP
    proc_int_enable = ARBGNT_REG_READ(s_ArbSema.pArbGntVirtAddr, COP_ENABLE);
    int_mask = arb_gnt & proc_int_enable;
    ARBGNT_REG_WRITE(s_ArbSema.pArbGntVirtAddr, 
        COP_ENABLE, (proc_int_enable & ~int_mask));
#else
    proc_int_enable = ARBGNT_REG_READ(s_ArbSema.pArbGntVirtAddr, CPU_ENABLE);
    int_mask = arb_gnt & proc_int_enable;
    ARBGNT_REG_WRITE(s_ArbSema.pArbGntVirtAddr, 
        CPU_ENABLE, (proc_int_enable & ~int_mask));
#endif
        
    //Signal all the required semaphores
    do
    {
        if (int_mask & 0x1)
        {
            NvOsSemaphoreSignal(s_ArbSema.semaphore[i]);
        }
        int_mask >>= 1;
        i++;
        
    } while (int_mask);

    NvOsIntrMutexUnlock(s_ArbSema.hIntrMutex);
}

NvU32 GetArbIdFromRmModuleId(NvRmModuleID modId)
{
    NvU32 arbId;

    switch(modId)
    {
        case NvRmModuleID_BseA:
            arbId = NvRmArbSema_Bsea;
            break;
        case NvRmModuleID_Vde:
        default:
            arbId = NvRmArbSema_Vde;
            break;
    }
    
    return arbId;
}

void NvRmXpcModuleAcquire(NvRmModuleID modId)
{
    NvU32 RequestedSemaNum;
    NvU32 reg;

    RequestedSemaNum = GetArbIdFromRmModuleId(modId);

    NvOsMutexLock(s_ArbSema.mutex[RequestedSemaNum]);
    NvOsIntrMutexLock(s_ArbSema.hIntrMutex);

    //Try to grab the lock
    ARBSEMA_REG_WRITE(s_ArbSema.pArbSemaVirtAddr, SMP_GET, 1 << RequestedSemaNum);

    //Enable arb sema interrupt
#if NV_IS_AVP
    reg = ARBGNT_REG_READ(s_ArbSema.pArbGntVirtAddr, COP_ENABLE);
    reg |= (1 << RequestedSemaNum);
    ARBGNT_REG_WRITE(s_ArbSema.pArbGntVirtAddr, COP_ENABLE, reg);
#else
    reg = ARBGNT_REG_READ(s_ArbSema.pArbGntVirtAddr, CPU_ENABLE);
    reg |= (1 << RequestedSemaNum);
    ARBGNT_REG_WRITE(s_ArbSema.pArbGntVirtAddr, CPU_ENABLE, reg);
#endif

    NvOsIntrMutexUnlock(s_ArbSema.hIntrMutex);
    NvOsSemaphoreWait(s_ArbSema.semaphore[RequestedSemaNum]);
}

void NvRmXpcModuleRelease(NvRmModuleID modId)
{
    NvU32 RequestedSemaNum;

    RequestedSemaNum = GetArbIdFromRmModuleId(modId);

    //Release the lock
    ARBSEMA_REG_WRITE(s_ArbSema.pArbSemaVirtAddr, SMP_PUT, 1 << RequestedSemaNum);

    NvOsMutexUnlock(s_ArbSema.mutex[RequestedSemaNum]);
}
