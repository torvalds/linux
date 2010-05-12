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

#include "nvreftrack.h"
#include "nvos.h"
#include "nvassert.h"

#define NVRT_MAX_PACKAGES              8
#define NVRT_MAX_OBJ_TYPES_PER_PACKAGE 8
#define NVRT_CLIENT_SIZE_INCR          16
#define NVRT_OBJ_SIZE_INCR             128

typedef struct
{
    // linked list next ptr (live and free objects)
    NvU32           NextObj;
    // the opaque ptr identifier of the object 
    void*           Ptr;
} NvRtObj;

typedef struct
{
    union
    {
        // linked list next ptr for free list, -1 == none
        NvU32       NextFree;
        // in use client refcount, -1 == cleaning up
        NvS32       RefCount;
    } State;
    
    void*           UserData;
    
    // lists of objects per obj type. array size can't
    // be determined compile time so this is not declared
    //NvU32           Objs[];
} NvRtClient;

typedef struct NvRtRec
{
    NvOsMutexHandle Mutex;

    NvU32           NumPackages;
    NvU32           MaxTypesPerPkg;
    NvU32*          ObjTypeIdxLUT;
    NvU32           NumObjTypes;
    
    NvU8*           ClientArr;
    NvU32           ClientArrSize;
    NvU32           FreeClientList;

    NvRtObj*        ObjArr;
    NvU32           ObjArrSize;
    NvU32           FreeObjList;
} NvRt;

static NV_INLINE NvU32
NvRtClientSize(NvRt* Rt)
{
    return sizeof(NvRtClient) + Rt->NumObjTypes*sizeof(NvU32);
}

static NV_INLINE NvRtClient*
GetClient(NvRt* Rt, NvU32 Idx)
{
    void* ptr = (void*)(Rt->ClientArr + Idx*NvRtClientSize(Rt));
    return (NvRtClient*)ptr;
}

static NV_INLINE NvU32
GetObjTypeIdx(NvRt* Rt, NvU32 Package, NvU32 Type)
{
    NvU32 LutIdx = Package*Rt->MaxTypesPerPkg + Type;
    NvU32 Idx;

    Idx = Rt->ObjTypeIdxLUT[LutIdx];
    NV_ASSERT(Idx != (NvU32)-1);

    return Idx;
}

static NV_INLINE NvU32*
GetObjListHead(NvRt* Rt, NvU32 ClientIdx, NvU32 ObjIdx)
{
    NvRtClient* Client = GetClient(Rt, ClientIdx);
    NvU32* Objs = (NvU32*)(Client + 1);
    return Objs + ObjIdx;
}

// Temporary wrapper for realloc as the linux kernel nvos doesn't
// implement NvOsRealloc
static NV_INLINE void*
NvRtRealloc(void* old, size_t size, size_t oldsize)
{
#if NVOS_IS_LINUX_KERNEL
    void* ret;
    
    if (!size)
    {
        if (old) NvOsFree(old);
        return NULL;
    }

    ret = NvOsAlloc(size);
    
    if (ret && old)
    {
        NV_ASSERT(oldsize > 0);

        NvOsMemcpy(ret, old, NV_MIN(size, oldsize));
        NvOsFree(old);
    }

    return ret;
#else
    return NvOsRealloc(old, size);
#endif
}

NvError NvRtCreate(
    NvU32 NumPackages,
    const NvU32* NumObjTypesPerPackage,
    NvRtHandle* RtOut)
{
    NvRtHandle Ctx;
    NvU32 i;

    if (NumPackages == 0)
    {
        NV_ASSERT(!"Zero packages is not allowed");
        return NvError_BadParameter;
    }
    
    if (NumPackages > NVRT_MAX_PACKAGES)
    {
        NV_ASSERT(!"NumPackages exceeds NVRT_MAX_PACKAGES");
        return NvError_BadParameter;
    }
    
    Ctx = NvOsAlloc(sizeof(NvRt));
    if (!Ctx) return NvError_InsufficientMemory;
    NvOsMemset(Ctx, 0, sizeof(NvRt));

    Ctx->FreeClientList = -1;    
    Ctx->FreeObjList    = -1;
    Ctx->NumPackages    = NumPackages;
        
    for (i = 0; i < NumPackages; i++)
    {
        if (NumObjTypesPerPackage[i] >
            NVRT_MAX_OBJ_TYPES_PER_PACKAGE)
        {
            NV_ASSERT(!"Too many object types");
            NvOsFree(Ctx);
            return NvError_BadParameter;
        }

        Ctx->NumObjTypes += NumObjTypesPerPackage[i];

        if (NumObjTypesPerPackage[i] > Ctx->MaxTypesPerPkg)
            Ctx->MaxTypesPerPkg = NumObjTypesPerPackage[i];
    }

    if (Ctx->MaxTypesPerPkg)
    {
        NvU32 idx = 0;
        
        Ctx->ObjTypeIdxLUT = NvOsAlloc(sizeof(NvU32)*Ctx->MaxTypesPerPkg*NumPackages);
        if (!Ctx->ObjTypeIdxLUT)
        {
            NvOsFree(Ctx);
            return NvError_InsufficientMemory;
        }

        for (i = 0; i < NumPackages; i++)
        {
            NvU32 start = i*Ctx->MaxTypesPerPkg;
            NvU32 j = 0;

            for (; j < NumObjTypesPerPackage[i]; j++)
            {
                Ctx->ObjTypeIdxLUT[start+j] = idx++;
            }
            for (; j < Ctx->MaxTypesPerPkg; j++)
            {
                Ctx->ObjTypeIdxLUT[start+j] = (NvU32)-1;
            }
        }
    }        
    
    if (NvOsMutexCreate(&Ctx->Mutex) != NvSuccess)
    {
        NvOsFree(Ctx->ObjTypeIdxLUT);
        NvOsFree(Ctx);
        return NvError_InsufficientMemory;
    }
    
    *RtOut = Ctx;
    return NvSuccess;
}

void NvRtDestroy(NvRtHandle Rt)
{
    NvOsMutexDestroy(Rt->Mutex);
    NvOsFree(Rt->ObjTypeIdxLUT);
    NvOsFree(Rt);
}

NvError NvRtRegisterClient(
    NvRtHandle Rt,
    NvRtClientHandle* ClientOut)
{
    NvOsMutexLock(Rt->Mutex);
    
    // Allocate new clients if necessary
    
    if (Rt->FreeClientList == -1)
    {
        NvU8* NewArr;
        NvU32 NewSize;
        NvU32 i;
        
        // Grow array by increment

        NewSize = Rt->ClientArrSize + NVRT_CLIENT_SIZE_INCR;
        NewArr = NvRtRealloc(Rt->ClientArr,
                             NvRtClientSize(Rt)*NewSize,
                             NvRtClientSize(Rt)*Rt->ClientArrSize);
        if (NewArr == NULL)
        {
            NvOsMutexUnlock(Rt->Mutex);
            return NvError_InsufficientMemory;
        }
        Rt->ClientArr = NewArr;

        // Initialize new clients and create free list

        for (i = Rt->ClientArrSize; i < NewSize; i++)
        {
            NvRtClient* c = GetClient(Rt, i);
            NvU32* objs = (NvU32*)(c+1);
            NvU32 j;

            c->State.NextFree = (i == NewSize-1) ? -1 : i+1;

            for (j = 0; j < Rt->NumObjTypes; j++)
                objs[j] = -1;            
        }
                    
        Rt->FreeClientList = Rt->ClientArrSize;
        Rt->ClientArrSize = NewSize;
    }

    NV_ASSERT(Rt->FreeClientList != -1);

    {
        NvU32       ClientIdx = Rt->FreeClientList;
        NvRtClient* Client    = GetClient(Rt, ClientIdx);
    
        Rt->FreeClientList = Client->State.NextFree;

        NvOsMutexUnlock(Rt->Mutex);

        // Initialize client
        
        Client->State.RefCount = 1;
        Client->UserData = NULL;
    
        *ClientOut = ClientIdx + 1;
    }

    return NvSuccess;
}

NvError NvRtAddClientRef(
    NvRtHandle Rt,
    NvRtClientHandle ClientHandle)
{
    NvRtClient* Client;
    NvU32       ClientIdx = ClientHandle - 1;
    NvError     Ret = NvSuccess;

    NV_ASSERT(ClientHandle != 0);
    NV_ASSERT(ClientHandle <= Rt->ClientArrSize);

    NvOsMutexLock(Rt->Mutex);

    Client = GetClient(Rt, ClientIdx);

    if (Client->State.RefCount < 1)
        Ret = NvError_InvalidState;
    else
        Client->State.RefCount++;

    NvOsMutexUnlock(Rt->Mutex);

    return Ret;
}

NvBool NvRtUnregisterClient(
    NvRtHandle Rt,
    NvRtClientHandle ClientHandle)
{
    NvRtClient* Client;
    NvU32       ClientIdx = ClientHandle - 1;
    NvU32*      Objs;
    NvU32       i;

    NV_ASSERT(ClientHandle != 0);
    NV_ASSERT(ClientHandle <= Rt->ClientArrSize);

    NvOsMutexLock(Rt->Mutex);

    Client = GetClient(Rt, ClientIdx);
    Client->State.RefCount--;
    
    if (Client->State.RefCount >= 0)
    {
        NvBool DoClean = (Client->State.RefCount == 0);
        NvOsMutexUnlock(Rt->Mutex);
        return DoClean;
    }    

    Objs = (NvU32*)(Client+1);

    // Check that object references are free'd
    
    for (i = Rt->NumObjTypes; i != 0; i--)
    {
        NvU32 Idx = i - 1;
        NvU32 Cur = Objs[Idx];

        // The caller should free all object referenced before
        // unregistering. Assert that this is so.
        
        NV_ASSERT(Cur == -1 || !"Leaked object reference");

        // In release builds free at least our state for the leaked
        // objects. There's nothing we can do about the leaked objects.
        
        while (Cur != -1)
        {
            NvRtObj* Obj = &Rt->ObjArr[Cur];
            NvU32 Next = Obj->NextObj;

            Obj->NextObj = Rt->FreeObjList;
            Rt->FreeObjList = Cur;
            Cur = Next;
        }

        Objs[Idx] = -1;
    }

    // Release client

    Client->State.NextFree = Rt->FreeClientList;
    Rt->FreeClientList = ClientIdx;

    NvOsMutexUnlock(Rt->Mutex);

    return NV_FALSE;
}

void NvRtSetClientUserData(
    NvRtHandle Rt,
    NvRtClientHandle ClientHandle,
    void* UserData)
{
    NvRtClient* Client;
    NvU32       ClientIdx = ClientHandle - 1;

    NV_ASSERT(ClientHandle != 0);
    NV_ASSERT(ClientHandle <= Rt->ClientArrSize);

    NvOsMutexLock(Rt->Mutex);

    Client = GetClient(Rt, ClientIdx);
    Client->UserData = UserData;
    
    NvOsMutexUnlock(Rt->Mutex);
}

void* NvRtGetClientUserData(
    NvRtHandle Rt,
    NvRtClientHandle ClientHandle)
{
    NvRtClient* Client;
    NvU32       ClientIdx = ClientHandle - 1;
    void*       UserData;

    NV_ASSERT(ClientHandle != 0);
    NV_ASSERT(ClientHandle <= Rt->ClientArrSize);

    NvOsMutexLock(Rt->Mutex);

    Client = GetClient(Rt, ClientIdx);
    UserData = Client->UserData;
    
    NvOsMutexUnlock(Rt->Mutex);

    return UserData;
}

NvError NvRtAllocObjRef(
    const NvDispatchCtx* Ctx,
    NvRtObjRefHandle* Out)
{
    NvRt*    Rt = Ctx->Rt;
    NvU32    ObjIdx;
    NvRtObj* Obj;
    
    NvOsMutexLock(Rt->Mutex);

    // Allocate new space if necessary
    
    if (Rt->FreeObjList == -1)
    {
        NvRtObj* NewArr;
        NvRtObj* Cur;
        NvU32 NewSize;
        NvU32 i;
        
        // Grow array by increment

        NewSize = Rt->ObjArrSize + NVRT_OBJ_SIZE_INCR;
        NewArr = NvRtRealloc(Rt->ObjArr,
                             sizeof(NvRtObj)*NewSize,
                             sizeof(NvRtObj)*Rt->ObjArrSize);
        if (NewArr == NULL)
        {
            NvOsMutexUnlock(Rt->Mutex);
            return NvError_InsufficientMemory;
        }

        // Create free list

        Cur = NewArr + Rt->ObjArrSize;
        for (i = Rt->ObjArrSize + 1; i < NewSize; i++)
        {
            Cur->NextObj = i;
            Cur++;
        }
        Cur->NextObj = -1;
            
        // Store new values
        
        Rt->ObjArr = NewArr;
        Rt->FreeObjList = Rt->ObjArrSize;
        Rt->ObjArrSize = NewSize;
    }

    NV_ASSERT(Rt->FreeObjList != -1);
    
    ObjIdx = Rt->FreeObjList;
    Obj = &Rt->ObjArr[ObjIdx];
    Rt->FreeObjList = Obj->NextObj;
            
    Obj->NextObj = -1;
    Obj->Ptr = NULL;

    NvOsMutexUnlock(Rt->Mutex);

    *Out = ObjIdx + 1;
    return NvSuccess;
}

void NvRtDiscardObjRef(
    const NvDispatchCtx* Ctx,
    NvRtObjRefHandle ObjRef)
{
    NvRt* Rt = Ctx->Rt;
    NvRtObj* Obj;

    if (!ObjRef--) return;
    
    NvOsMutexLock(Rt->Mutex);

    Obj = &Rt->ObjArr[ObjRef];

    NV_ASSERT(Obj->NextObj == -1);
    NV_ASSERT(Obj->Ptr == NULL);

    Obj->NextObj = Rt->FreeObjList;
    Rt->FreeObjList = ObjRef;

    NvOsMutexUnlock(Rt->Mutex);
}

void NvRtStoreObjRef(
    const NvDispatchCtx* Ctx,
    NvRtObjRefHandle ObjRef,
    NvU32 ObjType,
    void* ObjPtr)
{
    NvRt*    Rt         = Ctx->Rt;
    NvU32    ClientIdx  = Ctx->Client - 1;
    NvU32    ObjTypeIdx = GetObjTypeIdx(Rt, Ctx->PackageIdx, ObjType);
    NvRtObj* Obj;
    NvU32*   List;

    NV_ASSERT(ClientIdx < Rt->ClientArrSize);

    if (ObjPtr == NULL)
    {
        NV_ASSERT(!"Bad object ptr");
        return;
    }
    
    if (!ObjRef--)
    {
        NV_ASSERT(!"Bad object ref handle");
        return;
    }
        
    NvOsMutexLock(Rt->Mutex);

    Obj = &Rt->ObjArr[ObjRef];

    NV_ASSERT(Obj->NextObj == -1);
    NV_ASSERT(Obj->Ptr == NULL);

    List = GetObjListHead(Rt, ClientIdx, ObjTypeIdx);

    Obj->NextObj = *List;
    Obj->Ptr = ObjPtr;

    *List = ObjRef;    

    NvOsMutexUnlock(Rt->Mutex);    
}

void* NvRtFreeObjRef(
    const NvDispatchCtx* Ctx,
    NvU32 ObjType,
    void* ObjPtr)
{
    NvRt*  Rt         = Ctx->Rt;
    NvU32  ClientIdx  = Ctx->Client - 1;
    NvU32  ObjTypeIdx = GetObjTypeIdx(Rt, Ctx->PackageIdx, ObjType);
    NvU32  PrevIdx;
    NvU32  CurIdx;
    NvU32* List;
    void*  RetVal     = NULL;

    NV_ASSERT(ClientIdx < Rt->ClientArrSize);
    
    NvOsMutexLock(Rt->Mutex);

    List = GetObjListHead(Rt, ClientIdx, ObjTypeIdx);
    CurIdx = *List;
    PrevIdx = -1;

    // If user requested to find a specific object look it up
    
    if (ObjPtr != NULL)
    {
        while (CurIdx != -1)
        {
            NvRtObj* Obj = &Rt->ObjArr[CurIdx];

            if (Obj->Ptr == ObjPtr) break;

            PrevIdx = CurIdx;
            CurIdx = Obj->NextObj;
        }

        // User should not ask to free non-existent objects
        
        if (CurIdx == -1)
        {
            NV_ASSERT(!"Trying to free non-existent object reference");
            NvOsMutexUnlock(Rt->Mutex);
            return NULL;
        }                
    }

    // If we have an object, free it

    if (CurIdx != -1)
    {
        NvRtObj* Obj = &Rt->ObjArr[CurIdx];

        RetVal = Obj->Ptr;
        
        if (PrevIdx == -1)
        {
            *List = Obj->NextObj;
        }
        else
        {            
            NvRtObj* PrevObj = &Rt->ObjArr[PrevIdx];
            PrevObj->NextObj = Obj->NextObj;
        }

        Obj->Ptr = NULL;
        Obj->NextObj = Rt->FreeObjList;
        Rt->FreeObjList = CurIdx;        
    }
    
    NvOsMutexUnlock(Rt->Mutex);

    return RetVal;
}

#include <linux/module.h>

EXPORT_SYMBOL(NvRtAllocObjRef);
EXPORT_SYMBOL(NvRtDiscardObjRef);
EXPORT_SYMBOL(NvRtFreeObjRef);
EXPORT_SYMBOL(NvRtStoreObjRef);

EXPORT_SYMBOL(NvRtCreate);
EXPORT_SYMBOL(NvRtDestroy);
EXPORT_SYMBOL(NvRtRegisterClient);
EXPORT_SYMBOL(NvRtUnregisterClient);
