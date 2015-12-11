/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2015 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2015 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#ifndef __gc_hal_rename_h_
#define __gc_hal_rename_h_


#if defined(_HAL2D_APPENDIX)

#define _HAL2D_RENAME_2(api, appendix)  api ## appendix
#define _HAL2D_RENAME_1(api, appendix)  _HAL2D_RENAME_2(api, appendix)
#define gcmHAL2D(api)                   _HAL2D_RENAME_1(api, _HAL2D_APPENDIX)


#define gckOS_Construct                 gcmHAL2D(gckOS_Construct)
#define gckOS_Destroy                   gcmHAL2D(gckOS_Destroy)
#define gckOS_QueryVideoMemory          gcmHAL2D(gckOS_QueryVideoMemory)
#define gckOS_Allocate                  gcmHAL2D(gckOS_Allocate)
#define gckOS_Free                      gcmHAL2D(gckOS_Free)
#define gckOS_AllocateMemory            gcmHAL2D(gckOS_AllocateMemory)
#define gckOS_FreeMemory                gcmHAL2D(gckOS_FreeMemory)
#define gckOS_AllocatePagedMemory       gcmHAL2D(gckOS_AllocatePagedMemory)
#define gckOS_AllocatePagedMemoryEx     gcmHAL2D(gckOS_AllocatePagedMemoryEx)
#define gckOS_LockPages                 gcmHAL2D(gckOS_LockPages)
#define gckOS_MapPages                  gcmHAL2D(gckOS_MapPages)
#define gckOS_UnlockPages               gcmHAL2D(gckOS_UnlockPages)
#define gckOS_FreePagedMemory           gcmHAL2D(gckOS_FreePagedMemory)
#define gckOS_AllocateNonPagedMemory    gcmHAL2D(gckOS_AllocateNonPagedMemory)
#define gckOS_FreeNonPagedMemory        gcmHAL2D(gckOS_FreeNonPagedMemory)
#define gckOS_AllocateContiguous        gcmHAL2D(gckOS_AllocateContiguous)
#define gckOS_FreeContiguous            gcmHAL2D(gckOS_FreeContiguous)
#define gckOS_GetPageSize               gcmHAL2D(gckOS_GetPageSize)
#define gckOS_GetPhysicalAddress        gcmHAL2D(gckOS_GetPhysicalAddress)
#define gckOS_UserLogicalToPhysical     gcmHAL2D(gckOS_UserLogicalToPhysical)
#define gckOS_GetPhysicalAddressProcess gcmHAL2D(gckOS_GetPhysicalAddressProcess)
#define gckOS_MapPhysical               gcmHAL2D(gckOS_MapPhysical)
#define gckOS_UnmapPhysical             gcmHAL2D(gckOS_UnmapPhysical)
#define gckOS_ReadRegister              gcmHAL2D(gckOS_ReadRegister)
#define gckOS_WriteRegister             gcmHAL2D(gckOS_WriteRegister)
#define gckOS_WriteMemory               gcmHAL2D(gckOS_WriteMemory)
#define gckOS_MapMemory                 gcmHAL2D(gckOS_MapMemory)
#define gckOS_UnmapMemory               gcmHAL2D(gckOS_UnmapMemory)
#define gckOS_UnmapMemoryEx             gcmHAL2D(gckOS_UnmapMemoryEx)
#define gckOS_CreateMutex               gcmHAL2D(gckOS_CreateMutex)
#define gckOS_DeleteMutex               gcmHAL2D(gckOS_DeleteMutex)
#define gckOS_AcquireMutex              gcmHAL2D(gckOS_AcquireMutex)
#define gckOS_ReleaseMutex              gcmHAL2D(gckOS_ReleaseMutex)
#define gckOS_AtomicExchange            gcmHAL2D(gckOS_AtomicExchange)
#define gckOS_AtomicExchangePtr         gcmHAL2D(gckOS_AtomicExchangePtr)
#define gckOS_AtomConstruct             gcmHAL2D(gckOS_AtomConstruct)
#define gckOS_AtomDestroy               gcmHAL2D(gckOS_AtomDestroy)
#define gckOS_AtomGet                   gcmHAL2D(gckOS_AtomGet)
#define gckOS_AtomIncrement             gcmHAL2D(gckOS_AtomIncrement)
#define gckOS_AtomDecrement             gcmHAL2D(gckOS_AtomDecrement)
#define gckOS_Delay                     gcmHAL2D(gckOS_Delay)
#define gckOS_GetTime                   gcmHAL2D(gckOS_GetTime)
#define gckOS_MemoryBarrier             gcmHAL2D(gckOS_MemoryBarrier)
#define gckOS_MapUserPointer            gcmHAL2D(gckOS_MapUserPointer)
#define gckOS_UnmapUserPointer          gcmHAL2D(gckOS_UnmapUserPointer)
#define gckOS_QueryNeedCopy             gcmHAL2D(gckOS_QueryNeedCopy)
#define gckOS_CopyFromUserData          gcmHAL2D(gckOS_CopyFromUserData)
#define gckOS_CopyToUserData            gcmHAL2D(gckOS_CopyToUserData)
#define gckOS_SuspendInterrupt          gcmHAL2D(gckOS_SuspendInterrupt)
#define gckOS_ResumeInterrupt           gcmHAL2D(gckOS_ResumeInterrupt)
#define gckOS_GetBaseAddress            gcmHAL2D(gckOS_GetBaseAddress)
#define gckOS_MemCopy                   gcmHAL2D(gckOS_MemCopy)
#define gckOS_ZeroMemory                gcmHAL2D(gckOS_ZeroMemory)
#define gckOS_DeviceControl             gcmHAL2D(gckOS_DeviceControl)
#define gckOS_GetProcessID              gcmHAL2D(gckOS_GetProcessID)
#define gckOS_GetThreadID               gcmHAL2D(gckOS_GetThreadID)
#define gckOS_CreateSignal              gcmHAL2D(gckOS_CreateSignal)
#define gckOS_DestroySignal             gcmHAL2D(gckOS_DestroySignal)
#define gckOS_Signal                    gcmHAL2D(gckOS_Signal)
#define gckOS_WaitSignal                gcmHAL2D(gckOS_WaitSignal)
#define gckOS_MapSignal                 gcmHAL2D(gckOS_MapSignal)
#define gckOS_MapUserMemory             gcmHAL2D(gckOS_MapUserMemory)
#define gckOS_UnmapUserMemory           gcmHAL2D(gckOS_UnmapUserMemory)
#define gckOS_CreateUserSignal          gcmHAL2D(gckOS_CreateUserSignal)
#define gckOS_DestroyUserSignal         gcmHAL2D(gckOS_DestroyUserSignal)
#define gckOS_WaitUserSignal            gcmHAL2D(gckOS_WaitUserSignal)
#define gckOS_SignalUserSignal          gcmHAL2D(gckOS_SignalUserSignal)
#define gckOS_UserSignal                gcmHAL2D(gckOS_UserSignal)
#define gckOS_UserSignal                gcmHAL2D(gckOS_UserSignal)
#define gckOS_CacheClean                gcmHAL2D(gckOS_CacheClean)
#define gckOS_CacheFlush                gcmHAL2D(gckOS_CacheFlush)
#define gckOS_SetDebugLevel             gcmHAL2D(gckOS_SetDebugLevel)
#define gckOS_SetDebugZone              gcmHAL2D(gckOS_SetDebugZone)
#define gckOS_SetDebugLevelZone         gcmHAL2D(gckOS_SetDebugLevelZone)
#define gckOS_SetDebugZones             gcmHAL2D(gckOS_SetDebugZones)
#define gckOS_SetDebugFile              gcmHAL2D(gckOS_SetDebugFile)
#define gckOS_Broadcast                 gcmHAL2D(gckOS_Broadcast)
#define gckOS_SetGPUPower               gcmHAL2D(gckOS_SetGPUPower)
#define gckOS_CreateSemaphore           gcmHAL2D(gckOS_CreateSemaphore)
#define gckOS_DestroySemaphore          gcmHAL2D(gckOS_DestroySemaphore)
#define gckOS_AcquireSemaphore          gcmHAL2D(gckOS_AcquireSemaphore)
#define gckOS_ReleaseSemaphore          gcmHAL2D(gckOS_ReleaseSemaphore)
#define gckHEAP_Construct               gcmHAL2D(gckHEAP_Construct)
#define gckHEAP_Destroy                 gcmHAL2D(gckHEAP_Destroy)
#define gckHEAP_Allocate                gcmHAL2D(gckHEAP_Allocate)
#define gckHEAP_Free                    gcmHAL2D(gckHEAP_Free)
#define gckHEAP_ProfileStart            gcmHAL2D(gckHEAP_ProfileStart)
#define gckHEAP_ProfileEnd              gcmHAL2D(gckHEAP_ProfileEnd)
#define gckHEAP_Test                    gcmHAL2D(gckHEAP_Test)
#define gckVIDMEM_Construct             gcmHAL2D(gckVIDMEM_Construct)
#define gckVIDMEM_Destroy               gcmHAL2D(gckVIDMEM_Destroy)
#define gckVIDMEM_Allocate              gcmHAL2D(gckVIDMEM_Allocate)
#define gckVIDMEM_AllocateLinear        gcmHAL2D(gckVIDMEM_AllocateLinear)
#define gckVIDMEM_Free                  gcmHAL2D(gckVIDMEM_Free)
#define gckVIDMEM_Lock                  gcmHAL2D(gckVIDMEM_Lock)
#define gckVIDMEM_Unlock                gcmHAL2D(gckVIDMEM_Unlock)
#define gckVIDMEM_ConstructVirtual      gcmHAL2D(gckVIDMEM_ConstructVirtual)
#define gckVIDMEM_DestroyVirtual        gcmHAL2D(gckVIDMEM_DestroyVirtual)
#define gckKERNEL_Construct             gcmHAL2D(gckKERNEL_Construct)
#define gckKERNEL_Destroy               gcmHAL2D(gckKERNEL_Destroy)
#define gckKERNEL_Dispatch              gcmHAL2D(gckKERNEL_Dispatch)
#define gckKERNEL_QueryVideoMemory      gcmHAL2D(gckKERNEL_QueryVideoMemory)
#define gckKERNEL_GetVideoMemoryPool    gcmHAL2D(gckKERNEL_GetVideoMemoryPool)
#define gckKERNEL_MapVideoMemory        gcmHAL2D(gckKERNEL_MapVideoMemory)
#define gckKERNEL_UnmapVideoMemory      gcmHAL2D(gckKERNEL_UnmapVideoMemory)
#define gckKERNEL_MapMemory             gcmHAL2D(gckKERNEL_MapMemory)
#define gckKERNEL_UnmapMemory           gcmHAL2D(gckKERNEL_UnmapMemory)
#define gckKERNEL_Notify                gcmHAL2D(gckKERNEL_Notify)
#define gckKERNEL_QuerySettings         gcmHAL2D(gckKERNEL_QuerySettings)
#define gckKERNEL_Recovery              gcmHAL2D(gckKERNEL_Recovery)
#define gckKERNEL_OpenUserData          gcmHAL2D(gckKERNEL_OpenUserData)
#define gckKERNEL_CloseUserData         gcmHAL2D(gckKERNEL_CloseUserData)
#define gckHARDWARE_Construct           gcmHAL2D(gckHARDWARE_Construct)
#define gckHARDWARE_Destroy             gcmHAL2D(gckHARDWARE_Destroy)
#define gckHARDWARE_QuerySystemMemory   gcmHAL2D(gckHARDWARE_QuerySystemMemory)
#define gckHARDWARE_BuildVirtualAddress     gcmHAL2D(gckHARDWARE_BuildVirtualAddress)
#define gckHARDWARE_QueryCommandBuffer      gcmHAL2D(gckHARDWARE_QueryCommandBuffer)
#define gckHARDWARE_WaitLink            gcmHAL2D(gckHARDWARE_WaitLink)
#define gckHARDWARE_Execute             gcmHAL2D(gckHARDWARE_Execute)
#define gckHARDWARE_End                 gcmHAL2D(gckHARDWARE_End)
#define gckHARDWARE_Nop                 gcmHAL2D(gckHARDWARE_Nop)
#define gckHARDWARE_PipeSelect          gcmHAL2D(gckHARDWARE_PipeSelect)
#define gckHARDWARE_Link                gcmHAL2D(gckHARDWARE_Link)
#define gckHARDWARE_Event               gcmHAL2D(gckHARDWARE_Event)
#define gckHARDWARE_QueryMemory         gcmHAL2D(gckHARDWARE_QueryMemory)
#define gckHARDWARE_QueryChipIdentity   gcmHAL2D(gckHARDWARE_QueryChipIdentity)
#define gckHARDWARE_QueryChipSpecs      gcmHAL2D(gckHARDWARE_QueryChipSpecs)
#define gckHARDWARE_QueryShaderCaps     gcmHAL2D(gckHARDWARE_QueryShaderCaps)
#define gckHARDWARE_ConvertFormat       gcmHAL2D(gckHARDWARE_ConvertFormat)
#define gckHARDWARE_SplitMemory         gcmHAL2D(gckHARDWARE_SplitMemory)
#define gckHARDWARE_AlignToTile         gcmHAL2D(gckHARDWARE_AlignToTile)
#define gckHARDWARE_UpdateQueueTail     gcmHAL2D(gckHARDWARE_UpdateQueueTail)
#define gckHARDWARE_ConvertLogical      gcmHAL2D(gckHARDWARE_ConvertLogical)
#define gckHARDWARE_Interrupt           gcmHAL2D(gckHARDWARE_Interrupt)
#define gckHARDWARE_SetMMU              gcmHAL2D(gckHARDWARE_SetMMU)
#define gckHARDWARE_FlushMMU            gcmHAL2D(gckHARDWARE_FlushMMU)
#define gckHARDWARE_GetIdle             gcmHAL2D(gckHARDWARE_GetIdle)
#define gckHARDWARE_Flush               gcmHAL2D(gckHARDWARE_Flush)
#define gckHARDWARE_SetFastClear        gcmHAL2D(gckHARDWARE_SetFastClear)
#define gckHARDWARE_ReadInterrupt       gcmHAL2D(gckHARDWARE_ReadInterrupt)
#define gckHARDWARE_SetPowerManagementState         gcmHAL2D(gckHARDWARE_SetPowerManagementState)
#define gckHARDWARE_QueryPowerManagementState       gcmHAL2D(gckHARDWARE_QueryPowerManagementState)
#define gckHARDWARE_ProfileEngine2D     gcmHAL2D(gckHARDWARE_ProfileEngine2D)
#define gckHARDWARE_InitializeHardware  gcmHAL2D(gckHARDWARE_InitializeHardware)
#define gckHARDWARE_Reset               gcmHAL2D(gckHARDWARE_Reset)
#define gckINTERRUPT_Construct          gcmHAL2D(gckINTERRUPT_Construct)
#define gckINTERRUPT_Destroy            gcmHAL2D(gckINTERRUPT_Destroy)
#define gckINTERRUPT_SetHandler         gcmHAL2D(gckINTERRUPT_SetHandler)
#define gckINTERRUPT_Notify             gcmHAL2D(gckINTERRUPT_Notify)
#define gckEVENT_Construct              gcmHAL2D(gckEVENT_Construct)
#define gckEVENT_Destroy                gcmHAL2D(gckEVENT_Destroy)
#define gckEVENT_AddList                gcmHAL2D(gckEVENT_AddList)
#define gckEVENT_FreeNonPagedMemory     gcmHAL2D(gckEVENT_FreeNonPagedMemory)
#define gckEVENT_FreeContiguousMemory   gcmHAL2D(gckEVENT_FreeContiguousMemory)
#define gckEVENT_FreeVideoMemory        gcmHAL2D(gckEVENT_FreeVideoMemory)
#define gckEVENT_Signal                 gcmHAL2D(gckEVENT_Signal)
#define gckEVENT_Unlock                 gcmHAL2D(gckEVENT_Unlock)
#define gckEVENT_Submit                 gcmHAL2D(gckEVENT_Submit)
#define gckEVENT_Commit                 gcmHAL2D(gckEVENT_Commit)
#define gckEVENT_Notify                 gcmHAL2D(gckEVENT_Notify)
#define gckEVENT_Interrupt              gcmHAL2D(gckEVENT_Interrupt)
#define gckCOMMAND_Construct            gcmHAL2D(gckCOMMAND_Construct)
#define gckCOMMAND_Destroy              gcmHAL2D(gckCOMMAND_Destroy)
#define gckCOMMAND_EnterCommit          gcmHAL2D(gckCOMMAND_EnterCommit)
#define gckCOMMAND_ExitCommit           gcmHAL2D(gckCOMMAND_ExitCommit)
#define gckCOMMAND_Start                gcmHAL2D(gckCOMMAND_Start)
#define gckCOMMAND_Stop                 gcmHAL2D(gckCOMMAND_Stop)
#define gckCOMMAND_Commit               gcmHAL2D(gckCOMMAND_Commit)
#define gckCOMMAND_Reserve              gcmHAL2D(gckCOMMAND_Reserve)
#define gckCOMMAND_Execute              gcmHAL2D(gckCOMMAND_Execute)
#define gckCOMMAND_Stall                gcmHAL2D(gckCOMMAND_Stall)
#define gckCOMMAND_Attach               gcmHAL2D(gckCOMMAND_Attach)
#define gckCOMMAND_Detach               gcmHAL2D(gckCOMMAND_Detach)
#define gckMMU_Construct                gcmHAL2D(gckMMU_Construct)
#define gckMMU_Destroy                  gcmHAL2D(gckMMU_Destroy)
#define gckMMU_AllocatePages            gcmHAL2D(gckMMU_AllocatePages)
#define gckMMU_FreePages                gcmHAL2D(gckMMU_FreePages)
#define gckMMU_Test                     gcmHAL2D(gckMMU_Test)
#define gckHARDWARE_QueryProfileRegisters     gcmHAL2D(gckHARDWARE_QueryProfileRegisters)


#define FindMdlMap                      gcmHAL2D(FindMdlMap)
#define OnProcessExit                   gcmHAL2D(OnProcessExit)

#define gckGALDEVICE_Destroy            gcmHAL2D(gckGALDEVICE_Destroy)
#define gckOS_Print                     gcmHAL2D(gckOS_Print)
#define gckGALDEVICE_FreeMemory         gcmHAL2D(gckGALDEVICE_FreeMemory)
#define gckGALDEVICE_AllocateMemory     gcmHAL2D(gckGALDEVICE_AllocateMemory)
#define gckOS_DebugBreak                gcmHAL2D(gckOS_DebugBreak)
#define gckGALDEVICE_Release_ISR        gcmHAL2D(gckGALDEVICE_Release_ISR)
#define gckOS_Verify                    gcmHAL2D(gckOS_Verify)
#define gckCOMMAND_Release              gcmHAL2D(gckCOMMAND_Release)
#define gckGALDEVICE_Stop               gcmHAL2D(gckGALDEVICE_Stop)
#define gckGALDEVICE_Construct          gcmHAL2D(gckGALDEVICE_Construct)
#define gckOS_DebugFatal                gcmHAL2D(gckOS_DebugFatal)
#define gckOS_DebugTrace                gcmHAL2D(gckOS_DebugTrace)
#define gckHARDWARE_GetBaseAddress      gcmHAL2D(gckHARDWARE_GetBaseAddress)
#define gckGALDEVICE_Setup_ISR          gcmHAL2D(gckGALDEVICE_Setup_ISR)
#define gckKERNEL_AttachProcess         gcmHAL2D(gckKERNEL_AttachProcess)
#define gckKERNEL_AttachProcessEx       gcmHAL2D(gckKERNEL_AttachProcessEx)
#define gckGALDEVICE_Start_Thread       gcmHAL2D(gckGALDEVICE_Start_Thread)
#define gckHARDWARE_QueryIdle           gcmHAL2D(gckHARDWARE_QueryIdle)
#define gckGALDEVICE_Start              gcmHAL2D(gckGALDEVICE_Start)
#define gckOS_GetKernelLogical          gcmHAL2D(gckOS_GetKernelLogical)
#define gckOS_DebugTraceZone            gcmHAL2D(gckOS_DebugTraceZone)
#define gckGALDEVICE_Stop_Thread        gcmHAL2D(gckGALDEVICE_Stop_Thread)
#define gckHARDWARE_NeedBaseAddress     gcmHAL2D(gckHARDWARE_NeedBaseAddress)

#endif

#endif /* __gc_hal_rename_h_ */
