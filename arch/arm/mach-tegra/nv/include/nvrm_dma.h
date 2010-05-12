/*
 * Copyright (c) 2009 NVIDIA Corporation.
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

#ifndef INCLUDED_nvrm_dma_H
#define INCLUDED_nvrm_dma_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_module.h"
#include "nvrm_init.h"

/** 
 * @file
 * @brief <b>nVIDIA Driver Development Kit: 
 *           DMA Resource manager </b>
 *
 * @b Description: Defines the interface to the NvRM DMA.
 * 
 */

/**
 * @defgroup nvrm_dma Direct Memory Access (DMA) Controller API
 * 
 * This is the Dma interface.  These API provides the data transfer from memory
 * to the selected destination and vice versa. The one end is the memory and 
 * other end is the module selected by the dma module Id.
 * This API allocates the channel based on priority request. Higher priority 
 * channel can not be shared by other dma requestors. The low priority channel 
 * is shared between the different requestors.
 *
 * @ingroup nvddk_rm
 * 
 * @{
 */

#include "nvos.h"

/** 
 * NvRmDmaHandle is an opaque context to the NvRmDmaRec interface
 */

typedef struct NvRmDmaRec *NvRmDmaHandle;

/**
 * @brief Defines the DMA capability structure for getting the capability of 
 * the data transfer and any limitation if the dma manager have.
 */

typedef struct NvRmDmaCapabilitiesRec
{

    /// Holds the granularity of the data length for dma transfer in bytes
        NvU32 DmaGranularitySize;

    /// Holds the information if there is any address alignment limitation
    /// is available in term of bytes.  if this value is 1 then there is no
    /// limitation, any dma can transfer the data from any address. If this
    /// value is 2 then the address should be 2 byte aligned always to do
    /// the dma  transfer. If this value is 4 
    /// then the address should be 4 byte aligned always to do the dma
    /// transfer.
        NvU32 DmaAddressAlignmentSize;
} NvRmDmaCapabilities;

/**
 * @brief Defines the DMA client buffer information which is transferred 
 * recently. The direction of data transfer decides based on this address. The 
 * source address and destination address should be in line with the source 
 * module Id and destination module Id.
 */

typedef struct NvRmDmaClientBufferRec
{

    /// Specifies the dma source buffer physical address for dma transfer. 
        NvRmPhysAddr SourceBufferPhyAddress;

    /// Specifies the dma destination buffer physical address for dma transfer.
        NvRmPhysAddr DestinationBufferPhyAddress;

    /// Source address wrap size in bytes. It tells that after how much bytes, 
    /// it will be wrapped.
    /// If it is zero then wrapping for source address is disabled.
        NvU32 SourceAddressWrapSize;

    /// Destination address wrap size in bytes. It tells that after how much 
    /// bytes, it will be wrapped. If it is zero then wrapping for destination 
    /// address is disabled.
        NvU32 DestinationAddressWrapSize;

    /// Specifies the size of the buffer in bytes which is requested for
    /// transfer.
        NvU32 TransferSize;
} NvRmDmaClientBuffer;

/**
 * @brief Specify the name of modules which can be supported by nvrm dma
 * drivers.  These dma modules can be either source or destination based on
 * direction.
 */

typedef enum
{

    /// Specifies the dma module Id as Invalid
        NvRmDmaModuleID_Invalid = 0x0,

    /// Specifies the dma module Id for memory
        NvRmDmaModuleID_Memory,

    /// Specifies the dma module Id for I2s controller.
        NvRmDmaModuleID_I2s,

    /// Specifies the dma module Id for Ac97 controller.
        NvRmDmaModuleID_Ac97,

    /// Specifies the dma module Id for Spdif controller.
        NvRmDmaModuleID_Spdif,

    /// Specifies the dma module Id for uart controller.
        NvRmDmaModuleID_Uart,

    /// Specifies the dma module Id for Vfir controller.
        NvRmDmaModuleID_Vfir,

    /// Specifies the dma module Id for Mipi controller.
        NvRmDmaModuleID_Mipi,

    /// Specifies the dma module Id for spi controller.
        NvRmDmaModuleID_Spi,

    /// Specifies the dma module Id for slink controller.
        NvRmDmaModuleID_Slink,

    /// Specifies the dma module Id for I2c controller.
        NvRmDmaModuleID_I2c,

    /// Specifies the dma module Id for Dvc I2c controller.
        NvRmDmaModuleID_Dvc,

    /// Specifies the maximum number of modules supported.
        NvRmDmaModuleID_Max,
    NvRmDmaModuleID_Num,
    NvRmDmaModuleID_Force32 = 0x7FFFFFFF
} NvRmDmaModuleID;

/**
 * @brief Specify the direction of the transfer, either outbound data
 * (source -> dest) or inboud data (source <- dest)
 */

typedef enum
{

    /// Specifies the direction of the transfer to be srcdevice -> dstdevice
        NvRmDmaDirection_Forward = 0x1,

    /// Specifies the direction of the transfer to be dstdevice -> srcdevice
        NvRmDmaDirection_Reverse,
    NvRmDmaDirection_Num,
    NvRmDmaDirection_Force32 = 0x7FFFFFFF
} NvRmDmaDirection;

/**
 * @brief Specify the priority of the dma either low priority or high priority.
 */

typedef enum
{

    /// Low priority DMA, no guarantee of latency to start transactions
        NvRmDmaPriority_Low = 0x1,

    /// High priority DMA guarantees the first buffer you send the 
    /// NvRmDmaStartDmaTransfer() will begin immediately.
        NvRmDmaPriority_High,
    NvRmDmaPriority_Num,
    NvRmDmaPriority_Force32 = 0x7FFFFFFF
} NvRmDmaPriority;

/**
 * @brief Get the capabilities of the dma channels.
 * 
 * @param hDevice Handle to RM device.
 * @param pRmDmaCaps Pointer to the capability structure where the cpas value 
 * will be stored.
 *
 * @retval NvSuccess Indicates the function completed successfully.
 */

 NvError NvRmDmaGetCapabilities( 
    NvRmDeviceHandle hDevice,
    NvRmDmaCapabilities * pRmDmaCaps );

/**
 * @brief Allocate the DMA channel for the data transfer. The dma is allocated
 * based on the dma device Id information. Most of the configuration is also
 * done based on the source/destination device Id during the channel
 * allocation.  It initializes the channel also with standard configuration
 * based on source/ destination device.  The data is transferred from memory to
 * the dma requestor device or vice versa.  The dma requestors device can be
 * memory or any peripheral device listed in the NvRmDmaDeviceId.
 * 
 * Assert encountered in debug mode if passed parameter is invalid.
 *
 * @param hRmDevice Handle to RM device.
 * @param phDma Pointer to the dma handle where the allocated dma handle
 * will be stored.
 * @param Enable32bitSwap if set to NV_TRUE will unconditionally reverse the
 * memory order of bytes on 4-byte chunks.  D3:D2:D1:D0 becomes D0:D1:D2:D3
 * @param Priority Selects either Hi or Low priority.  A Low priority
 * allocation will only fail if the system is out of memory, and transfers on a
 * Low priority channel will be intermixed with other clients of that channel.
 * Hi priority allocations may fail if there is not a dedicated channel
 * available for the Hi priority client.  Hi priority channels should only be
 * used if you have very specific latency requirements.
 * @param DmaRequestorModuleId Specifies a source module Id.
 * @param DmaRequestorInstanceId Specifies the instance of the source module.
 * 
 * @retval NvSuccess Indicates the function completed successfully.
 * @retval NvDMAChannelNotAvailable Indicates that there is no channel
 * available for allocation.
 * @retval NvError_InsufficientMemory Indicates that it will not able to
 * allocate the memory for dma handles.
 * @retval NvDMAInvalidSourceId Indicates that device requested is not the
 * valid device.
 * @retval NvError_MemoryMapFailed Indicates that the memory mapping for 
 * controller register failed.
 * @retval NvError_MutexCreateFailed Indicates that the creation of mutex
 * failed.  Mutex is required to provide the thread safety.
 * @retval NvError_SemaphoreCreateFailed Indicates that the creation of 
 * semaphore failed. Semaphore is required to provide the synchronization and 
 * also used in synchronous operation.
 *
 */

 NvError NvRmDmaAllocate( 
    NvRmDeviceHandle hRmDevice,
    NvRmDmaHandle * phDma,
    NvBool Enable32bitSwap,
    NvRmDmaPriority Priority,
    NvRmDmaModuleID DmaRequestorModuleId,
    NvU32 DmaRequestorInstanceId );

/**
 * Frees the channel so that it can be reused by other clients.  This function
 * will block until all currently enqueued transfers complete.
 *
 * @note: We may change the functionality so that Free() returns immediately
 * but internally the channel remains in an alloc'd state until all transfers
 * complete.
 *
 * @param hDma A DMA handle from NvRmDmaAllocate.  If hDma is NULL, this API has
 *     no effect.
 */

 void NvRmDmaFree( 
    NvRmDmaHandle hDma );

/**
 * @brief Starts the DMA channel for data transfer.
 * 
 * Assert encountered in debug mode if passed parameter is invalid.
 *
 * @param hDma Specifies a DMA handle which is allocated by the Rm dma from 
 * NvRmDmaAllocate.
 * @param pClientBuffer Specifies a pointer to the client information which 
 * contains the start buffer, destination buffer, and number of bytes 
 * transferred.
 * @param DmaDirection Specifies whether the transfer is Forward src->dst or
 * Reverse dst->src direction.
 * @param WaitTimeoutInMilliSecond The time need to wait in milliseconds. If it
 * is zero then it will be returned immediately as asynchronous operation. If
 * is non zero then it will wait for a requested timeout. If it is
 * NV_WAIT_INFINITE then it will wait for infinitely till transaction
 * completes.
 * @param AsynchSemaphoreId The semaphore Id which need to be signal if client 
 * is requested for asynchronous operation.  Pass NULL if not semaphore should 
 * be signalled when the transfer is complete.
 * 
 * @retval NvSuccess Indicates the function completed successfully.
 * @retval NvError_InvalidAddress Indicates that the address for source or 
 * destination is invalid.
 * @retval NvError_InvalidSize Indicates that the bytes requested is invalid.
 * @retval NvError_Timeout Indicates that transfer is not completed in a
 * expected time and timeout happen.
 */

 NvError NvRmDmaStartDmaTransfer( 
    NvRmDmaHandle hDma,
    NvRmDmaClientBuffer * pClientBuffer,
    NvRmDmaDirection DmaDirection,
    NvU32 WaitTimeoutInMilliSecond,
    NvOsSemaphoreHandle AsynchSemaphoreId );

/**
 * @brief Aborts the currently running transfer as well as any other transfers 
 * that are queued up behind the currently running transfer.
 * 
 * @param hDma Specifies a DMA handle which is allocated by the Rm dma from 
 * NvRmDmaAllocate.
 */

 void NvRmDmaAbort( 
    NvRmDmaHandle hDma );

/**
 * @brief Get the number of bytes transferred by the dma in current tranaction 
 * from the last.
 * 
 * This will tell the number of bytes has been transferred by the dma yet from 
 * the last transfer completes.
 *
 * @param hDma Specifies a DMA handle which is allocated by the Rm dma from 
 * NvRmDmaAllocate.
 * @param pTransferCount Pointer to the variable where number of bytes transferred 
 * by dma will be stored.
 * @param IsTransferStop Tells whether the current transfer is stopped or not.
 *
 * @retval NvSuccess Indicates the function completed successfully.
 * @retval NvError_InvalidState The transfer is not going on.
 */

 NvError NvRmDmaGetTransferredCount( 
    NvRmDmaHandle hDma,
    NvU32 * pTransferCount,
    NvBool IsTransferStop );

/**
 * @brief Tells whether the transfer is completed or not for the given dma transfer.
 * 
 * This will tells the first or second half of the buffer transfer for the requestor
 * who uses the double buffering mechanism like i2s.
 *
 * @param hDma Specifies a DMA handle which is allocated by the Rm dma from 
 * NvRmDmaAllocate.
 * @param IsFirstHalfBuffer Tells whether the first half or second half of the dma transfer.
 *
 * @retval NV_TRUE indicates that the transfre has been completed.
 * @retval NV_FALSE Indicates that the transfre is going on.
 */

 NvBool NvRmDmaIsDmaTransferCompletes( 
    NvRmDmaHandle hDma,
    NvBool IsFirstHalfBuffer );

#if defined(__cplusplus)
}
#endif

#endif
