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
#include "nvrm_memctrl.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))


typedef struct NvRmCorePerfMonStop_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
    NvU32  * pCountList;
} NV_ALIGN(4) NvRmCorePerfMonStop_in;

typedef struct NvRmCorePerfMonStop_inout_t
{
    NvU32 pCountListSize;
} NV_ALIGN(4) NvRmCorePerfMonStop_inout;

typedef struct NvRmCorePerfMonStop_out_t
{
    NvError ret_;
    NvU32 pTotalCycleCount;
} NV_ALIGN(4) NvRmCorePerfMonStop_out;

typedef struct NvRmCorePerfMonStop_params_t
{
    NvRmCorePerfMonStop_in in;
    NvRmCorePerfMonStop_inout inout;
    NvRmCorePerfMonStop_out out;
} NvRmCorePerfMonStop_params;

typedef struct NvRmCorePerfMonStart_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
    NvU32  * pEventList;
} NV_ALIGN(4) NvRmCorePerfMonStart_in;

typedef struct NvRmCorePerfMonStart_inout_t
{
    NvU32 pEventListSize;
} NV_ALIGN(4) NvRmCorePerfMonStart_inout;

typedef struct NvRmCorePerfMonStart_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmCorePerfMonStart_out;

typedef struct NvRmCorePerfMonStart_params_t
{
    NvRmCorePerfMonStart_in in;
    NvRmCorePerfMonStart_inout inout;
    NvRmCorePerfMonStart_out out;
} NvRmCorePerfMonStart_params;

typedef struct ReadObsData_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle rm;
    NvRmModuleID modId;
    NvU32 start_index;
    NvU32 length;
} NV_ALIGN(4) ReadObsData_in;

typedef struct ReadObsData_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) ReadObsData_inout;

typedef struct ReadObsData_out_t
{
    NvError ret_;
    NvU32 value;
} NV_ALIGN(4) ReadObsData_out;

typedef struct ReadObsData_params_t
{
    ReadObsData_in in;
    ReadObsData_inout inout;
    ReadObsData_out out;
} ReadObsData_params;

typedef struct McStat_Report_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvU32 client_id_0;
    NvU32 client_0_cycles;
    NvU32 client_id_1;
    NvU32 client_1_cycles;
    NvU32 llc_client_id;
    NvU32 llc_client_clocks;
    NvU32 llc_client_cycles;
    NvU32 mc_clocks;
} NV_ALIGN(4) McStat_Report_in;

typedef struct McStat_Report_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) McStat_Report_inout;

typedef struct McStat_Report_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) McStat_Report_out;

typedef struct McStat_Report_params_t
{
    McStat_Report_in in;
    McStat_Report_inout inout;
    McStat_Report_out out;
} McStat_Report_params;

typedef struct McStat_Stop_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle rm;
} NV_ALIGN(4) McStat_Stop_in;

typedef struct McStat_Stop_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) McStat_Stop_inout;

typedef struct McStat_Stop_out_t
{
    NvU32 client_0_cycles;
    NvU32 client_1_cycles;
    NvU32 llc_client_cycles;
    NvU32 llc_client_clocks;
    NvU32 mc_clocks;
} NV_ALIGN(4) McStat_Stop_out;

typedef struct McStat_Stop_params_t
{
    McStat_Stop_in in;
    McStat_Stop_inout inout;
    McStat_Stop_out out;
} McStat_Stop_params;

typedef struct McStat_Start_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle rm;
    NvU32 client_id_0;
    NvU32 client_id_1;
    NvU32 llc_client_id;
} NV_ALIGN(4) McStat_Start_in;

typedef struct McStat_Start_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) McStat_Start_inout;

typedef struct McStat_Start_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) McStat_Start_out;

typedef struct McStat_Start_params_t
{
    McStat_Start_in in;
    McStat_Start_inout inout;
    McStat_Start_out out;
} McStat_Start_params;

static NvError NvRmCorePerfMonStop_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmCorePerfMonStop_in *p_in;
    NvRmCorePerfMonStop_inout *p_inout;
    NvRmCorePerfMonStop_out *p_out;
    NvRmCorePerfMonStop_inout inout;
    NvU32  *pCountList = NULL;

    p_in = (NvRmCorePerfMonStop_in *)InBuffer;
    p_inout = (NvRmCorePerfMonStop_inout *)((NvU8 *)InBuffer + OFFSET(NvRmCorePerfMonStop_params, inout));
    p_out = (NvRmCorePerfMonStop_out *)((NvU8 *)OutBuffer + OFFSET(NvRmCorePerfMonStop_params, out) - OFFSET(NvRmCorePerfMonStop_params, inout));

    (void)inout;
    inout.pCountListSize = p_inout->pCountListSize;
    if( p_inout->pCountListSize && p_in->pCountList )
    {
        pCountList = (NvU32  *)NvOsAlloc( p_inout->pCountListSize * sizeof( NvU32  ) );
        if( !pCountList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmCorePerfMonStop( p_in->hRmDevice, &inout.pCountListSize, pCountList, &p_out->pTotalCycleCount );


    p_inout = (NvRmCorePerfMonStop_inout *)OutBuffer;
    p_inout->pCountListSize = inout.pCountListSize;
    if(p_in->pCountList && pCountList)
    {
        err_ = NvOsCopyOut( p_in->pCountList, pCountList, p_inout->pCountListSize * sizeof( NvU32  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    NvOsFree( pCountList );
    return err_;
}

static NvError NvRmCorePerfMonStart_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmCorePerfMonStart_in *p_in;
    NvRmCorePerfMonStart_inout *p_inout;
    NvRmCorePerfMonStart_out *p_out;
    NvRmCorePerfMonStart_inout inout;
    NvU32  *pEventList = NULL;

    p_in = (NvRmCorePerfMonStart_in *)InBuffer;
    p_inout = (NvRmCorePerfMonStart_inout *)((NvU8 *)InBuffer + OFFSET(NvRmCorePerfMonStart_params, inout));
    p_out = (NvRmCorePerfMonStart_out *)((NvU8 *)OutBuffer + OFFSET(NvRmCorePerfMonStart_params, out) - OFFSET(NvRmCorePerfMonStart_params, inout));

    (void)inout;
    inout.pEventListSize = p_inout->pEventListSize;
    if( p_inout->pEventListSize && p_in->pEventList )
    {
        pEventList = (NvU32  *)NvOsAlloc( p_inout->pEventListSize * sizeof( NvU32  ) );
        if( !pEventList )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->pEventList )
        {
            err_ = NvOsCopyIn( pEventList, p_in->pEventList, p_inout->pEventListSize * sizeof( NvU32  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmCorePerfMonStart( p_in->hRmDevice, &inout.pEventListSize, pEventList );


    p_inout = (NvRmCorePerfMonStart_inout *)OutBuffer;
    p_inout->pEventListSize = inout.pEventListSize;
clean:
    NvOsFree( pEventList );
    return err_;
}

static NvError ReadObsData_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    ReadObsData_in *p_in;
    ReadObsData_out *p_out;

    p_in = (ReadObsData_in *)InBuffer;
    p_out = (ReadObsData_out *)((NvU8 *)OutBuffer + OFFSET(ReadObsData_params, out) - OFFSET(ReadObsData_params, inout));


    p_out->ret_ = ReadObsData( p_in->rm, p_in->modId, p_in->start_index, p_in->length, &p_out->value );

    return err_;
}

static NvError McStat_Report_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    McStat_Report_in *p_in;

    p_in = (McStat_Report_in *)InBuffer;


    McStat_Report( p_in->client_id_0, p_in->client_0_cycles, p_in->client_id_1, p_in->client_1_cycles, p_in->llc_client_id, p_in->llc_client_clocks, p_in->llc_client_cycles, p_in->mc_clocks );

    return err_;
}

static NvError McStat_Stop_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    McStat_Stop_in *p_in;
    McStat_Stop_out *p_out;

    p_in = (McStat_Stop_in *)InBuffer;
    p_out = (McStat_Stop_out *)((NvU8 *)OutBuffer + OFFSET(McStat_Stop_params, out) - OFFSET(McStat_Stop_params, inout));


    McStat_Stop( p_in->rm, &p_out->client_0_cycles, &p_out->client_1_cycles, &p_out->llc_client_cycles, &p_out->llc_client_clocks, &p_out->mc_clocks );

    return err_;
}

static NvError McStat_Start_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    McStat_Start_in *p_in;

    p_in = (McStat_Start_in *)InBuffer;


    McStat_Start( p_in->rm, p_in->client_id_0, p_in->client_id_1, p_in->llc_client_id );

    return err_;
}

NvError nvrm_memctrl_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_memctrl_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 5:
        err_ = NvRmCorePerfMonStop_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 4:
        err_ = NvRmCorePerfMonStart_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 3:
        err_ = ReadObsData_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = McStat_Report_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = McStat_Stop_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = McStat_Start_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
