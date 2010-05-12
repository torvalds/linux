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

#ifndef __NVSNOR_CONTROLLER_H
#define __NVSNOR_CONTROLLER_H

#include "mach/nvrm_linux.h"
#include "nvos.h"
#include "nvassert.h"
#include "nvodm_query.h"

#include "nvodm_services.h"
#include "nvodm_query_discovery.h"

#include "ap20/arsnor.h"
#include "nvrm_hardware_access.h"
#include "nvrm_power.h"
#include "nvrm_drf.h"
#include "nvrm_module.h"
#include "nvrm_memmgr.h"
#include "nvrm_interrupt.h"

#define SNOR_CONTROLLER_CHIPSELECT_MAX 8

#define SNOR_DMA_BUFFER_SIZE_BYTE 0x1000 //4KB
//#define SNOR_DMA_BUFFER_SIZE_BYTE 0x4000 //16KB


#define SNOR_READ32(pSnorHwRegsVirtBaseAdd, reg) \
        NV_READ32((pSnorHwRegsVirtBaseAdd) + ((SNOR_##reg##_0)/4))

#define SNOR_WRITE32(pSnorHwRegsVirtBaseAdd, reg, val) \
    do \
    {  \
        NV_WRITE32((((pSnorHwRegsVirtBaseAdd) + ((SNOR_##reg##_0)/4))), (val)); \
    } while (0)

typedef struct
{
   NvRmPhysAddr DeviceBaseAddress;   
   NvU32 DeviceAddressSize;  
   NvU16 *pDeviceBaseVirtAddress;  
   NvU32 DevicePureAddress;
} ConnectedDeviceIntRegister;

typedef struct 
{
    NvU32 Config;

    NvU32 Status;
    NvU32 NorAddressPtr;
    NvU32 AhbAddrPtr;
    NvU32 Timing0;
    NvU32 Timing1;
    NvU32 MioCfg;
    NvU32 MioTiming;
    NvU32 DmaConfig;
    NvU32 ChipSelectMuxConfig;
} SnorControllerRegs;

typedef struct 
{
    NvU32 Muxed_Width;
    NvU32 Hold_Width;
    NvU32 ADV_dWidth;
    NvU32 WE_Width;
    NvU32 OE_Width;
    NvU32 Wait_Width;
    
} SnorControllerTimingRegVals;


typedef struct NvSnorRec
{
    NvRmDeviceHandle hRmDevice;

    NvU32 OpenCount;
    
    // Physical Address of the SNOR controller instance
    NvU32 SnorControllerBaseAdd;

    // Virtual address for the SNOR controller instance
    NvU32 *pSnorControllerVirtBaseAdd;

    // Size of the SNOR register map
    NvU32 SnorRegMapSize;

    // Semaphore  for registering the client with the power manager.
    NvOsSemaphoreHandle hRmPowerEventSema;
    
    // Power client Id.
    NvU32 RmPowerClientId;
 
    // Command complete semaphore
    NvOsSemaphoreHandle hCommandCompleteSema;
    
    // Interrupt handle
    NvOsInterruptHandle hIntr;
    //For SNOR controller's DMA allocation
    NvRmMemHandle hRmMemory;
    NvRmPhysAddr DmaBuffPhysAdd;
    NvU32 *pAhbDmaBuffer;
    NvU32 Snor_DmaBufSize;
    
    //Number of devices present
    NvU32 NumOfDevicesConnected;
    
    // Tells whether the device is avialble or not.
    //NvU32 IsDevAvailable[SNOR_CONTROLLER_CHIPSELECT_MAX];

    // Device interface register to access the devices which is controlled by SNOR controller.
    ConnectedDeviceIntRegister ConnectedDevReg;

    SnorControllerRegs SnorRegs;
} NvSnor;

typedef struct NvSnorRec *NvSnorHandle;

typedef struct 
{
    NvRmDeviceHandle hRmDevice;
    NvSnorHandle hSnor;
} NvSnorInformation;


NvError InitSnorInformation(void);
void DeinitSnorInformation(void);
void InitSnorController(NvSnorHandle hSnor, NvU32 DevTypeSNOREn, SnorControllerTimingRegVals TimingRegVals);
void SetChipSelect(NvSnorHandle hSnor, NvU32 ChipSelId);
NvError CreateSnorHandle(NvRmDeviceHandle hRmDevice, NvSnorHandle *phSnor);
void DestroySnorHandle(NvSnorHandle hSnor);

void NvReadViaSNORControllerDMA (NvSnorHandle hSnor, void* SnorAddr, NvU32 word32bit_count);
void NvWriteViaSNORControllerDMA (NvSnorHandle hSnor, void* SnorAddr, NvU32 word32bit_count);

#endif
