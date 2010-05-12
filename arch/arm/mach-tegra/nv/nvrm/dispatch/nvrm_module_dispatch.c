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

#define NV_IDL_IS_DISPATCH

#include "nvcommon.h"
#include "nvos.h"
#include "nvassert.h"
#include "nvreftrack.h"
#include "nvidlcmd.h"
#include "nvrm_module.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRegw08_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle rm;
    NvRmModuleID aperture;
    NvU32 offset;
    NvU8 data;
} NV_ALIGN(4) NvRegw08_in;

typedef struct NvRegw08_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRegw08_inout;

typedef struct NvRegw08_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRegw08_out;

typedef struct NvRegw08_params_t
{
    NvRegw08_in in;
    NvRegw08_inout inout;
    NvRegw08_out out;
} NvRegw08_params;

typedef struct NvRegr08_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDeviceHandle;
    NvRmModuleID aperture;
    NvU32 offset;
} NV_ALIGN(4) NvRegr08_in;

typedef struct NvRegr08_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRegr08_inout;

typedef struct NvRegr08_out_t
{
    NvU8 ret_;
} NV_ALIGN(4) NvRegr08_out;

typedef struct NvRegr08_params_t
{
    NvRegr08_in in;
    NvRegr08_inout inout;
    NvRegr08_out out;
} NvRegr08_params;

typedef struct NvRegrb_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmModuleID aperture;
    NvU32 num;
    NvU32 offset;
    NvU32  * values;
} NV_ALIGN(4) NvRegrb_in;

typedef struct NvRegrb_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRegrb_inout;

typedef struct NvRegrb_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRegrb_out;

typedef struct NvRegrb_params_t
{
    NvRegrb_in in;
    NvRegrb_inout inout;
    NvRegrb_out out;
} NvRegrb_params;

typedef struct NvRegwb_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmModuleID aperture;
    NvU32 num;
    NvU32 offset;
    NvU32  * values;
} NV_ALIGN(4) NvRegwb_in;

typedef struct NvRegwb_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRegwb_inout;

typedef struct NvRegwb_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRegwb_out;

typedef struct NvRegwb_params_t
{
    NvRegwb_in in;
    NvRegwb_inout inout;
    NvRegwb_out out;
} NvRegwb_params;

typedef struct NvRegwm_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmModuleID aperture;
    NvU32 num;
    NvU32  * offsets;
    NvU32  * values;
} NV_ALIGN(4) NvRegwm_in;

typedef struct NvRegwm_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRegwm_inout;

typedef struct NvRegwm_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRegwm_out;

typedef struct NvRegwm_params_t
{
    NvRegwm_in in;
    NvRegwm_inout inout;
    NvRegwm_out out;
} NvRegwm_params;

typedef struct NvRegrm_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmModuleID aperture;
    NvU32 num;
    NvU32  * offsets;
    NvU32  * values;
} NV_ALIGN(4) NvRegrm_in;

typedef struct NvRegrm_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRegrm_inout;

typedef struct NvRegrm_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRegrm_out;

typedef struct NvRegrm_params_t
{
    NvRegrm_in in;
    NvRegrm_inout inout;
    NvRegrm_out out;
} NvRegrm_params;

typedef struct NvRegw_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDeviceHandle;
    NvRmModuleID aperture;
    NvU32 offset;
    NvU32 data;
} NV_ALIGN(4) NvRegw_in;

typedef struct NvRegw_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRegw_inout;

typedef struct NvRegw_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRegw_out;

typedef struct NvRegw_params_t
{
    NvRegw_in in;
    NvRegw_inout inout;
    NvRegw_out out;
} NvRegw_params;

typedef struct NvRegr_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDeviceHandle;
    NvRmModuleID aperture;
    NvU32 offset;
} NV_ALIGN(4) NvRegr_in;

typedef struct NvRegr_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRegr_inout;

typedef struct NvRegr_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRegr_out;

typedef struct NvRegr_params_t
{
    NvRegr_in in;
    NvRegr_inout inout;
    NvRegr_out out;
} NvRegr_params;

typedef struct NvRmGetRandomBytes_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvU32 NumBytes;
    void* pBytes;
} NV_ALIGN(4) NvRmGetRandomBytes_in;

typedef struct NvRmGetRandomBytes_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmGetRandomBytes_inout;

typedef struct NvRmGetRandomBytes_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmGetRandomBytes_out;

typedef struct NvRmGetRandomBytes_params_t
{
    NvRmGetRandomBytes_in in;
    NvRmGetRandomBytes_inout inout;
    NvRmGetRandomBytes_out out;
} NvRmGetRandomBytes_params;

typedef struct NvRmQueryChipUniqueId_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevHandle;
    NvU32 IdSize;
    void* pId;
} NV_ALIGN(4) NvRmQueryChipUniqueId_in;

typedef struct NvRmQueryChipUniqueId_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmQueryChipUniqueId_inout;

typedef struct NvRmQueryChipUniqueId_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmQueryChipUniqueId_out;

typedef struct NvRmQueryChipUniqueId_params_t
{
    NvRmQueryChipUniqueId_in in;
    NvRmQueryChipUniqueId_inout inout;
    NvRmQueryChipUniqueId_out out;
} NvRmQueryChipUniqueId_params;

typedef struct NvRmModuleGetCapabilities_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDeviceHandle;
    NvRmModuleID Module;
    NvRmModuleCapability  * pCaps;
    NvU32 NumCaps;
} NV_ALIGN(4) NvRmModuleGetCapabilities_in;

typedef struct NvRmModuleGetCapabilities_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmModuleGetCapabilities_inout;

typedef struct NvRmModuleGetCapabilities_out_t
{
    NvError ret_;
    void* Capability;
} NV_ALIGN(4) NvRmModuleGetCapabilities_out;

typedef struct NvRmModuleGetCapabilities_params_t
{
    NvRmModuleGetCapabilities_in in;
    NvRmModuleGetCapabilities_inout inout;
    NvRmModuleGetCapabilities_out out;
} NvRmModuleGetCapabilities_params;

typedef struct NvRmModuleResetWithHold_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmModuleID Module;
    NvBool bHold;
} NV_ALIGN(4) NvRmModuleResetWithHold_in;

typedef struct NvRmModuleResetWithHold_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmModuleResetWithHold_inout;

typedef struct NvRmModuleResetWithHold_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmModuleResetWithHold_out;

typedef struct NvRmModuleResetWithHold_params_t
{
    NvRmModuleResetWithHold_in in;
    NvRmModuleResetWithHold_inout inout;
    NvRmModuleResetWithHold_out out;
} NvRmModuleResetWithHold_params;

typedef struct NvRmModuleReset_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmModuleID Module;
} NV_ALIGN(4) NvRmModuleReset_in;

typedef struct NvRmModuleReset_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmModuleReset_inout;

typedef struct NvRmModuleReset_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmModuleReset_out;

typedef struct NvRmModuleReset_params_t
{
    NvRmModuleReset_in in;
    NvRmModuleReset_inout inout;
    NvRmModuleReset_out out;
} NvRmModuleReset_params;

typedef struct NvRmModuleGetNumInstances_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmModuleID Module;
} NV_ALIGN(4) NvRmModuleGetNumInstances_in;

typedef struct NvRmModuleGetNumInstances_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmModuleGetNumInstances_inout;

typedef struct NvRmModuleGetNumInstances_out_t
{
    NvU32 ret_;
} NV_ALIGN(4) NvRmModuleGetNumInstances_out;

typedef struct NvRmModuleGetNumInstances_params_t
{
    NvRmModuleGetNumInstances_in in;
    NvRmModuleGetNumInstances_inout inout;
    NvRmModuleGetNumInstances_out out;
} NvRmModuleGetNumInstances_params;

typedef struct NvRmModuleGetBaseAddress_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDeviceHandle;
    NvRmModuleID Module;
} NV_ALIGN(4) NvRmModuleGetBaseAddress_in;

typedef struct NvRmModuleGetBaseAddress_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmModuleGetBaseAddress_inout;

typedef struct NvRmModuleGetBaseAddress_out_t
{
    NvRmPhysAddr pBaseAddress;
    NvU32 pSize;
} NV_ALIGN(4) NvRmModuleGetBaseAddress_out;

typedef struct NvRmModuleGetBaseAddress_params_t
{
    NvRmModuleGetBaseAddress_in in;
    NvRmModuleGetBaseAddress_inout inout;
    NvRmModuleGetBaseAddress_out out;
} NvRmModuleGetBaseAddress_params;

typedef struct NvRmModuleGetModuleInfo_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hDevice;
    NvRmModuleID module;
    NvRmModuleInfo  * pModuleInfo;
} NV_ALIGN(4) NvRmModuleGetModuleInfo_in;

typedef struct NvRmModuleGetModuleInfo_inout_t
{
    NvU32 pNum;
} NV_ALIGN(4) NvRmModuleGetModuleInfo_inout;

typedef struct NvRmModuleGetModuleInfo_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmModuleGetModuleInfo_out;

typedef struct NvRmModuleGetModuleInfo_params_t
{
    NvRmModuleGetModuleInfo_in in;
    NvRmModuleGetModuleInfo_inout inout;
    NvRmModuleGetModuleInfo_out out;
} NvRmModuleGetModuleInfo_params;

static NvError NvRegw08_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRegw08_in *p_in;

    p_in = (NvRegw08_in *)InBuffer;


    NvRegw08( p_in->rm, p_in->aperture, p_in->offset, p_in->data );

    return err_;
}

static NvError NvRegr08_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRegr08_in *p_in;
    NvRegr08_out *p_out;

    p_in = (NvRegr08_in *)InBuffer;
    p_out = (NvRegr08_out *)((NvU8 *)OutBuffer + OFFSET(NvRegr08_params, out) - OFFSET(NvRegr08_params, inout));


    p_out->ret_ = NvRegr08( p_in->hDeviceHandle, p_in->aperture, p_in->offset );

    return err_;
}

static NvError NvRegrb_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRegrb_in *p_in;
    NvU32  *values = NULL;

    p_in = (NvRegrb_in *)InBuffer;

    if( p_in->num && p_in->values )
    {
        values = (NvU32  *)NvOsAlloc( p_in->num * sizeof( NvU32  ) );
        if( !values )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    NvRegrb( p_in->hRmDeviceHandle, p_in->aperture, p_in->num, p_in->offset, values );

    if(p_in->values && values)
    {
        err_ = NvOsCopyOut( p_in->values, values, p_in->num * sizeof( NvU32  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( values );
    return err_;
}

static NvError NvRegwb_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRegwb_in *p_in;
    NvU32  *values = NULL;

    p_in = (NvRegwb_in *)InBuffer;

    if( p_in->num && p_in->values )
    {
        values = (NvU32  *)NvOsAlloc( p_in->num * sizeof( NvU32  ) );
        if( !values )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->values )
        {
            err_ = NvOsCopyIn( values, p_in->values, p_in->num * sizeof( NvU32  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    NvRegwb( p_in->hRmDeviceHandle, p_in->aperture, p_in->num, p_in->offset, values );

clean:
    NvOsFree( values );
    return err_;
}

static NvError NvRegwm_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRegwm_in *p_in;
    NvU32  *offsets = NULL;
    NvU32  *values = NULL;

    p_in = (NvRegwm_in *)InBuffer;

    if( p_in->num && p_in->offsets )
    {
        offsets = (NvU32  *)NvOsAlloc( p_in->num * sizeof( NvU32  ) );
        if( !offsets )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->offsets )
        {
            err_ = NvOsCopyIn( offsets, p_in->offsets, p_in->num * sizeof( NvU32  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }
    if( p_in->num && p_in->values )
    {
        values = (NvU32  *)NvOsAlloc( p_in->num * sizeof( NvU32  ) );
        if( !values )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->values )
        {
            err_ = NvOsCopyIn( values, p_in->values, p_in->num * sizeof( NvU32  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    NvRegwm( p_in->hRmDeviceHandle, p_in->aperture, p_in->num, offsets, values );

clean:
    NvOsFree( offsets );
    NvOsFree( values );
    return err_;
}

static NvError NvRegrm_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRegrm_in *p_in;
    NvU32  *offsets = NULL;
    NvU32  *values = NULL;

    p_in = (NvRegrm_in *)InBuffer;

    if( p_in->num && p_in->offsets )
    {
        offsets = (NvU32  *)NvOsAlloc( p_in->num * sizeof( NvU32  ) );
        if( !offsets )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->offsets )
        {
            err_ = NvOsCopyIn( offsets, p_in->offsets, p_in->num * sizeof( NvU32  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }
    if( p_in->num && p_in->values )
    {
        values = (NvU32  *)NvOsAlloc( p_in->num * sizeof( NvU32  ) );
        if( !values )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    NvRegrm( p_in->hRmDeviceHandle, p_in->aperture, p_in->num, offsets, values );

    if(p_in->values && values)
    {
        err_ = NvOsCopyOut( p_in->values, values, p_in->num * sizeof( NvU32  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( offsets );
    NvOsFree( values );
    return err_;
}

static NvError NvRegw_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRegw_in *p_in;

    p_in = (NvRegw_in *)InBuffer;


    NvRegw( p_in->hDeviceHandle, p_in->aperture, p_in->offset, p_in->data );

    return err_;
}

static NvError NvRegr_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRegr_in *p_in;
    NvRegr_out *p_out;

    p_in = (NvRegr_in *)InBuffer;
    p_out = (NvRegr_out *)((NvU8 *)OutBuffer + OFFSET(NvRegr_params, out) - OFFSET(NvRegr_params, inout));


    p_out->ret_ = NvRegr( p_in->hDeviceHandle, p_in->aperture, p_in->offset );

    return err_;
}

static NvError NvRmGetRandomBytes_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmGetRandomBytes_in *p_in;
    NvRmGetRandomBytes_out *p_out;
    void*  pBytes = NULL;

    p_in = (NvRmGetRandomBytes_in *)InBuffer;
    p_out = (NvRmGetRandomBytes_out *)((NvU8 *)OutBuffer + OFFSET(NvRmGetRandomBytes_params, out) - OFFSET(NvRmGetRandomBytes_params, inout));

    if( p_in->NumBytes && p_in->pBytes )
    {
        pBytes = (void*  )NvOsAlloc( p_in->NumBytes );
        if( !pBytes )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmGetRandomBytes( p_in->hRmDeviceHandle, p_in->NumBytes, pBytes );

    if(p_in->pBytes && pBytes)
    {
        err_ = NvOsCopyOut( p_in->pBytes, pBytes, p_in->NumBytes );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( pBytes );
    return err_;
}

static NvError NvRmQueryChipUniqueId_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmQueryChipUniqueId_in *p_in;
    NvRmQueryChipUniqueId_out *p_out;
    void*  pId = NULL;

    p_in = (NvRmQueryChipUniqueId_in *)InBuffer;
    p_out = (NvRmQueryChipUniqueId_out *)((NvU8 *)OutBuffer + OFFSET(NvRmQueryChipUniqueId_params, out) - OFFSET(NvRmQueryChipUniqueId_params, inout));

    if( p_in->IdSize && p_in->pId )
    {
        pId = (void*  )NvOsAlloc( p_in->IdSize );
        if( !pId )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmQueryChipUniqueId( p_in->hDevHandle, p_in->IdSize, pId );

    if(p_in->pId && pId)
    {
        err_ = NvOsCopyOut( p_in->pId, pId, p_in->IdSize );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( pId );
    return err_;
}

static NvError NvRmModuleGetCapabilities_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmModuleGetCapabilities_in *p_in;
    NvRmModuleGetCapabilities_out *p_out;
    NvRmModuleCapability *pCaps = NULL;

    p_in = (NvRmModuleGetCapabilities_in *)InBuffer;
    p_out = (NvRmModuleGetCapabilities_out *)((NvU8 *)OutBuffer + OFFSET(NvRmModuleGetCapabilities_params, out) - OFFSET(NvRmModuleGetCapabilities_params, inout));

    if( p_in->NumCaps && p_in->pCaps )
    {
        pCaps = (NvRmModuleCapability  *)NvOsAlloc( p_in->NumCaps * sizeof( NvRmModuleCapability  ) );
        if( !pCaps )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->pCaps )
        {
            err_ = NvOsCopyIn( pCaps, p_in->pCaps, p_in->NumCaps * sizeof( NvRmModuleCapability  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmModuleGetCapabilities( p_in->hDeviceHandle, p_in->Module, pCaps, p_in->NumCaps, &p_out->Capability );

clean:
    NvOsFree( pCaps );
    return err_;
}

static NvError NvRmModuleResetWithHold_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmModuleResetWithHold_in *p_in;

    p_in = (NvRmModuleResetWithHold_in *)InBuffer;


    NvRmModuleResetWithHold( p_in->hRmDeviceHandle, p_in->Module, p_in->bHold );

    return err_;
}

static NvError NvRmModuleReset_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmModuleReset_in *p_in;

    p_in = (NvRmModuleReset_in *)InBuffer;


    NvRmModuleReset( p_in->hRmDeviceHandle, p_in->Module );

    return err_;
}

static NvError NvRmModuleGetNumInstances_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmModuleGetNumInstances_in *p_in;
    NvRmModuleGetNumInstances_out *p_out;

    p_in = (NvRmModuleGetNumInstances_in *)InBuffer;
    p_out = (NvRmModuleGetNumInstances_out *)((NvU8 *)OutBuffer + OFFSET(NvRmModuleGetNumInstances_params, out) - OFFSET(NvRmModuleGetNumInstances_params, inout));


    p_out->ret_ = NvRmModuleGetNumInstances( p_in->hRmDeviceHandle, p_in->Module );

    return err_;
}

static NvError NvRmModuleGetBaseAddress_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmModuleGetBaseAddress_in *p_in;
    NvRmModuleGetBaseAddress_out *p_out;

    p_in = (NvRmModuleGetBaseAddress_in *)InBuffer;
    p_out = (NvRmModuleGetBaseAddress_out *)((NvU8 *)OutBuffer + OFFSET(NvRmModuleGetBaseAddress_params, out) - OFFSET(NvRmModuleGetBaseAddress_params, inout));


    NvRmModuleGetBaseAddress( p_in->hRmDeviceHandle, p_in->Module, &p_out->pBaseAddress, &p_out->pSize );

    return err_;
}

static NvError NvRmModuleGetModuleInfo_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmModuleGetModuleInfo_in *p_in;
    NvRmModuleGetModuleInfo_inout *p_inout;
    NvRmModuleGetModuleInfo_out *p_out;
    NvRmModuleGetModuleInfo_inout inout;
    NvRmModuleInfo *pModuleInfo = NULL;

    p_in = (NvRmModuleGetModuleInfo_in *)InBuffer;
    p_inout = (NvRmModuleGetModuleInfo_inout *)((NvU8 *)InBuffer + OFFSET(NvRmModuleGetModuleInfo_params, inout));
    p_out = (NvRmModuleGetModuleInfo_out *)((NvU8 *)OutBuffer + OFFSET(NvRmModuleGetModuleInfo_params, out) - OFFSET(NvRmModuleGetModuleInfo_params, inout));

    (void)inout;
    inout.pNum = p_inout->pNum;
    if( p_inout->pNum && p_in->pModuleInfo )
    {
        pModuleInfo = (NvRmModuleInfo  *)NvOsAlloc( p_inout->pNum * sizeof( NvRmModuleInfo  ) );
        if( !pModuleInfo )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmModuleGetModuleInfo( p_in->hDevice, p_in->module, &inout.pNum, pModuleInfo );


    p_inout = (NvRmModuleGetModuleInfo_inout *)OutBuffer;
    p_inout->pNum = inout.pNum;
    if(p_in->pModuleInfo && pModuleInfo)
    {
        err_ = NvOsCopyOut( p_in->pModuleInfo, pModuleInfo, p_inout->pNum * sizeof( NvRmModuleInfo  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( pModuleInfo );
    return err_;
}

NvError nvrm_module_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_module_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 15:
        err_ = NvRegw08_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 14:
        err_ = NvRegr08_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 13:
        err_ = NvRegrb_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 12:
        err_ = NvRegwb_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 11:
        err_ = NvRegwm_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 10:
        err_ = NvRegrm_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 9:
        err_ = NvRegw_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 8:
        err_ = NvRegr_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 7:
        err_ = NvRmGetRandomBytes_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 6:
        err_ = NvRmQueryChipUniqueId_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 5:
        err_ = NvRmModuleGetCapabilities_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 4:
        err_ = NvRmModuleResetWithHold_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 3:
        err_ = NvRmModuleReset_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = NvRmModuleGetNumInstances_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmModuleGetBaseAddress_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmModuleGetModuleInfo_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
