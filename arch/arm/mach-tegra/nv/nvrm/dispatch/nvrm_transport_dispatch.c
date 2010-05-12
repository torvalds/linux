
#define NV_IDL_IS_DISPATCH

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

#include "nvcommon.h"
#include "nvos.h"
#include "nvassert.h"
#include "nvreftrack.h"
#include "nvidlcmd.h"
#include "nvrm_transport.h"

#define OFFSET( s, e ) (NvU32)(void *)(&(((s*)0)->e))
#define MAX_MESSAGE_LENGTH 256
#define MAX_PORT_NAME_LENGTH 20

typedef struct NvRmTransportRecvMsg_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmTransportHandle hTransport;
    void* pMessageBuffer;
    NvU32 MaxSize;
} NV_ALIGN(4) NvRmTransportRecvMsg_in;

typedef struct NvRmTransportRecvMsg_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmTransportRecvMsg_inout;

typedef struct NvRmTransportRecvMsg_out_t
{
    NvError ret_;
    NvU32 pMessageSize;
} NV_ALIGN(4) NvRmTransportRecvMsg_out;

typedef struct NvRmTransportRecvMsg_params_t
{
    NvRmTransportRecvMsg_in in;
    NvRmTransportRecvMsg_inout inout;
    NvRmTransportRecvMsg_out out;
} NvRmTransportRecvMsg_params;

typedef struct NvRmTransportSendMsgInLP0_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmTransportHandle hPort;
    void* message;
    NvU32 MessageSize;
} NV_ALIGN(4) NvRmTransportSendMsgInLP0_in;

typedef struct NvRmTransportSendMsgInLP0_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmTransportSendMsgInLP0_inout;

typedef struct NvRmTransportSendMsgInLP0_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmTransportSendMsgInLP0_out;

typedef struct NvRmTransportSendMsgInLP0_params_t
{
    NvRmTransportSendMsgInLP0_in in;
    NvRmTransportSendMsgInLP0_inout inout;
    NvRmTransportSendMsgInLP0_out out;
} NvRmTransportSendMsgInLP0_params;

typedef struct NvRmTransportSendMsg_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmTransportHandle hTransport;
    void* pMessageBuffer;
    NvU32 MessageSize;
    NvU32 TimeoutMS;
} NV_ALIGN(4) NvRmTransportSendMsg_in;

typedef struct NvRmTransportSendMsg_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmTransportSendMsg_inout;

typedef struct NvRmTransportSendMsg_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmTransportSendMsg_out;

typedef struct NvRmTransportSendMsg_params_t
{
    NvRmTransportSendMsg_in in;
    NvRmTransportSendMsg_inout inout;
    NvRmTransportSendMsg_out out;
} NvRmTransportSendMsg_params;

typedef struct NvRmTransportSetQueueDepth_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmTransportHandle hTransport;
    NvU32 MaxQueueDepth;
    NvU32 MaxMessageSize;
} NV_ALIGN(4) NvRmTransportSetQueueDepth_in;

typedef struct NvRmTransportSetQueueDepth_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmTransportSetQueueDepth_inout;

typedef struct NvRmTransportSetQueueDepth_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmTransportSetQueueDepth_out;

typedef struct NvRmTransportSetQueueDepth_params_t
{
    NvRmTransportSetQueueDepth_in in;
    NvRmTransportSetQueueDepth_inout inout;
    NvRmTransportSetQueueDepth_out out;
} NvRmTransportSetQueueDepth_params;

typedef struct NvRmTransportConnect_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmTransportHandle hTransport;
    NvU32 TimeoutMS;
} NV_ALIGN(4) NvRmTransportConnect_in;

typedef struct NvRmTransportConnect_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmTransportConnect_inout;

typedef struct NvRmTransportConnect_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmTransportConnect_out;

typedef struct NvRmTransportConnect_params_t
{
    NvRmTransportConnect_in in;
    NvRmTransportConnect_inout inout;
    NvRmTransportConnect_out out;
} NvRmTransportConnect_params;

typedef struct NvRmTransportWaitForConnect_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmTransportHandle hTransport;
    NvU32 TimeoutMS;
} NV_ALIGN(4) NvRmTransportWaitForConnect_in;

typedef struct NvRmTransportWaitForConnect_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmTransportWaitForConnect_inout;

typedef struct NvRmTransportWaitForConnect_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmTransportWaitForConnect_out;

typedef struct NvRmTransportWaitForConnect_params_t
{
    NvRmTransportWaitForConnect_in in;
    NvRmTransportWaitForConnect_inout inout;
    NvRmTransportWaitForConnect_out out;
} NvRmTransportWaitForConnect_params;

typedef struct NvRmTransportDeInit_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
} NV_ALIGN(4) NvRmTransportDeInit_in;

typedef struct NvRmTransportDeInit_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmTransportDeInit_inout;

typedef struct NvRmTransportDeInit_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmTransportDeInit_out;

typedef struct NvRmTransportDeInit_params_t
{
    NvRmTransportDeInit_in in;
    NvRmTransportDeInit_inout inout;
    NvRmTransportDeInit_out out;
} NvRmTransportDeInit_params;

typedef struct NvRmTransportInit_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
} NV_ALIGN(4) NvRmTransportInit_in;

typedef struct NvRmTransportInit_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmTransportInit_inout;

typedef struct NvRmTransportInit_out_t
{
    NvError ret_;
} NV_ALIGN(4) NvRmTransportInit_out;

typedef struct NvRmTransportInit_params_t
{
    NvRmTransportInit_in in;
    NvRmTransportInit_inout inout;
    NvRmTransportInit_out out;
} NvRmTransportInit_params;

typedef struct NvRmTransportClose_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmTransportHandle hTransport;
} NV_ALIGN(4) NvRmTransportClose_in;

typedef struct NvRmTransportClose_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmTransportClose_inout;

typedef struct NvRmTransportClose_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmTransportClose_out;

typedef struct NvRmTransportClose_params_t
{
    NvRmTransportClose_in in;
    NvRmTransportClose_inout inout;
    NvRmTransportClose_out out;
} NvRmTransportClose_params;

typedef struct NvRmTransportGetPortName_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmTransportHandle hTransport;
    NvU8  * PortName;
    NvU32 PortNameSize;
} NV_ALIGN(4) NvRmTransportGetPortName_in;

typedef struct NvRmTransportGetPortName_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmTransportGetPortName_inout;

typedef struct NvRmTransportGetPortName_out_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmTransportGetPortName_out;

typedef struct NvRmTransportGetPortName_params_t
{
    NvRmTransportGetPortName_in in;
    NvRmTransportGetPortName_inout inout;
    NvRmTransportGetPortName_out out;
} NvRmTransportGetPortName_params;

typedef struct NvRmTransportOpen_in_t
{
    NvU32 package_;
    NvU32 function_;
    NvRmDeviceHandle hRmDevice;
    char * pPortName_data;
    NvU32 pPortName_len;
    NvOsSemaphoreHandle RecvMessageSemaphore;
} NV_ALIGN(4) NvRmTransportOpen_in;

typedef struct NvRmTransportOpen_inout_t
{
    NvU32 dummy_;
} NV_ALIGN(4) NvRmTransportOpen_inout;

typedef struct NvRmTransportOpen_out_t
{
    NvError ret_;
    NvRmTransportHandle phTransport;
} NV_ALIGN(4) NvRmTransportOpen_out;

typedef struct NvRmTransportOpen_params_t
{
    NvRmTransportOpen_in in;
    NvRmTransportOpen_inout inout;
    NvRmTransportOpen_out out;
} NvRmTransportOpen_params;

static NvError NvRmTransportRecvMsg_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmTransportRecvMsg_in *p_in;
    NvRmTransportRecvMsg_out *p_out;
    void*  pMessageBuffer = NULL;
    NvU32 MsgBuff[MAX_MESSAGE_LENGTH/sizeof(NvU32)];

    p_in = (NvRmTransportRecvMsg_in *)InBuffer;
    p_out = (NvRmTransportRecvMsg_out *)((NvU8 *)OutBuffer + OFFSET(NvRmTransportRecvMsg_params, out) - OFFSET(NvRmTransportRecvMsg_params, inout));

    if( p_in->MaxSize && p_in->pMessageBuffer )
    {
        pMessageBuffer = (void*  )MsgBuff;
        if( p_in->MaxSize > MAX_MESSAGE_LENGTH )
            pMessageBuffer = (void* )NvOsAlloc( p_in->MaxSize );
        if( !pMessageBuffer )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
    }

    p_out->ret_ = NvRmTransportRecvMsg( p_in->hTransport, pMessageBuffer, p_in->MaxSize, &p_out->pMessageSize );

    if(p_in->pMessageBuffer && pMessageBuffer)
    {
        err_ = NvOsCopyOut( p_in->pMessageBuffer, pMessageBuffer, p_in->MaxSize );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    if (pMessageBuffer != MsgBuff)
        NvOsFree( pMessageBuffer );
    return err_;
}

static NvError NvRmTransportSendMsgInLP0_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmTransportSendMsgInLP0_in *p_in;
    NvRmTransportSendMsgInLP0_out *p_out;
    void*  message = NULL;
    NvU32 MsgBuff[MAX_MESSAGE_LENGTH/sizeof(NvU32)];

    p_in = (NvRmTransportSendMsgInLP0_in *)InBuffer;
    p_out = (NvRmTransportSendMsgInLP0_out *)((NvU8 *)OutBuffer + OFFSET(NvRmTransportSendMsgInLP0_params, out) - OFFSET(NvRmTransportSendMsgInLP0_params, inout));

    if( p_in->MessageSize && p_in->message )
    {
        message = (void*  )MsgBuff;
        if( p_in->MessageSize > MAX_MESSAGE_LENGTH )
            message = (void*  )NvOsAlloc( p_in->MessageSize );
        if( !message )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->message )
        {
            err_ = NvOsCopyIn( message, p_in->message, p_in->MessageSize );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmTransportSendMsgInLP0( p_in->hPort, message, p_in->MessageSize );

clean:
    if( message != MsgBuff )
        NvOsFree( message );
    return err_;
}

static NvError NvRmTransportSendMsg_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmTransportSendMsg_in *p_in;
    NvRmTransportSendMsg_out *p_out;
    void*  pMessageBuffer = NULL;
    NvU32 MsgBuff[MAX_MESSAGE_LENGTH/sizeof(NvU32)];

    p_in = (NvRmTransportSendMsg_in *)InBuffer;
    p_out = (NvRmTransportSendMsg_out *)((NvU8 *)OutBuffer + OFFSET(NvRmTransportSendMsg_params, out) - OFFSET(NvRmTransportSendMsg_params, inout));

    if( p_in->MessageSize && p_in->pMessageBuffer )
    {
        pMessageBuffer = (void*  )&MsgBuff[0];
        if( p_in->MessageSize > MAX_MESSAGE_LENGTH )
            pMessageBuffer = (void*  )NvOsAlloc( p_in->MessageSize );
        if( !pMessageBuffer )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->pMessageBuffer )
        {
            err_ = NvOsCopyIn( pMessageBuffer, p_in->pMessageBuffer, p_in->MessageSize );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    p_out->ret_ = NvRmTransportSendMsg( p_in->hTransport, pMessageBuffer, p_in->MessageSize, p_in->TimeoutMS );

clean:
    if( pMessageBuffer != MsgBuff )
        NvOsFree( pMessageBuffer );
    return err_;
}

static NvError NvRmTransportSetQueueDepth_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmTransportSetQueueDepth_in *p_in;
    NvRmTransportSetQueueDepth_out *p_out;

    p_in = (NvRmTransportSetQueueDepth_in *)InBuffer;
    p_out = (NvRmTransportSetQueueDepth_out *)((NvU8 *)OutBuffer + OFFSET(NvRmTransportSetQueueDepth_params, out) - OFFSET(NvRmTransportSetQueueDepth_params, inout));


    p_out->ret_ = NvRmTransportSetQueueDepth( p_in->hTransport, p_in->MaxQueueDepth, p_in->MaxMessageSize );

    return err_;
}

static NvError NvRmTransportConnect_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmTransportConnect_in *p_in;
    NvRmTransportConnect_out *p_out;

    p_in = (NvRmTransportConnect_in *)InBuffer;
    p_out = (NvRmTransportConnect_out *)((NvU8 *)OutBuffer + OFFSET(NvRmTransportConnect_params, out) - OFFSET(NvRmTransportConnect_params, inout));


    p_out->ret_ = NvRmTransportConnect( p_in->hTransport, p_in->TimeoutMS );

    return err_;
}

static NvError NvRmTransportWaitForConnect_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmTransportWaitForConnect_in *p_in;
    NvRmTransportWaitForConnect_out *p_out;

    p_in = (NvRmTransportWaitForConnect_in *)InBuffer;
    p_out = (NvRmTransportWaitForConnect_out *)((NvU8 *)OutBuffer + OFFSET(NvRmTransportWaitForConnect_params, out) - OFFSET(NvRmTransportWaitForConnect_params, inout));


    p_out->ret_ = NvRmTransportWaitForConnect( p_in->hTransport, p_in->TimeoutMS );

    return err_;
}

static NvError NvRmTransportDeInit_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmTransportDeInit_in *p_in;

    p_in = (NvRmTransportDeInit_in *)InBuffer;


    NvRmTransportDeInit( p_in->hRmDevice );

    return err_;
}

static NvError NvRmTransportInit_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmTransportInit_in *p_in;
    NvRmTransportInit_out *p_out;

    p_in = (NvRmTransportInit_in *)InBuffer;
    p_out = (NvRmTransportInit_out *)((NvU8 *)OutBuffer + OFFSET(NvRmTransportInit_params, out) - OFFSET(NvRmTransportInit_params, inout));


    p_out->ret_ = NvRmTransportInit( p_in->hRmDevice );

    return err_;
}

static NvError NvRmTransportClose_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmTransportClose_in *p_in;

    p_in = (NvRmTransportClose_in *)InBuffer;


    NvRmTransportClose( p_in->hTransport );

    return err_;
}

static NvError NvRmTransportGetPortName_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmTransportGetPortName_in *p_in;
    NvU8  *PortName = NULL;
    NvU32 PortNameBuff[MAX_PORT_NAME_LENGTH/sizeof(NvU32)];

    p_in = (NvRmTransportGetPortName_in *)InBuffer;

    if( p_in->PortNameSize && p_in->PortName )
    {
        PortName = (NvU8  *)PortNameBuff;
        if( (p_in->PortNameSize * sizeof(NvU8)) > MAX_PORT_NAME_LENGTH )
            PortName = (NvU8  *)NvOsAlloc( p_in->PortNameSize * sizeof( NvU8  ) );
        if( !PortName )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        if( p_in->PortName )
        {
            err_ = NvOsCopyIn( PortName, p_in->PortName, p_in->PortNameSize * sizeof( NvU8  ) );
            if( err_ != NvSuccess )
            {
                err_ = NvError_BadParameter;
                goto clean;
            }
        }
    }

    NvRmTransportGetPortName( p_in->hTransport, PortName, p_in->PortNameSize );

    if(p_in->PortName && PortName)
    {
        err_ = NvOsCopyOut( p_in->PortName, PortName, p_in->PortNameSize * sizeof( NvU8  ) );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
        }
    }
clean:
    if ( PortName != PortNameBuff )
        NvOsFree( PortName );
    return err_;
}

static NvError NvRmTransportOpen_dispatch_( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;
    NvRmTransportOpen_in *p_in;
    NvRmTransportOpen_out *p_out;
    char *pPortName = NULL;
    NvOsSemaphoreHandle RecvMessageSemaphore = NULL;
    NvU32 PortNameBuff[MAX_PORT_NAME_LENGTH/sizeof(NvU32)];

    p_in = (NvRmTransportOpen_in *)InBuffer;
    p_out = (NvRmTransportOpen_out *)((NvU8 *)OutBuffer + OFFSET(NvRmTransportOpen_params, out) - OFFSET(NvRmTransportOpen_params, inout));

    if( p_in->pPortName_len )
    {
        pPortName = (char *)PortNameBuff;
        if( p_in->pPortName_len > MAX_PORT_NAME_LENGTH )
            pPortName = NvOsAlloc( p_in->pPortName_len );
        if( !pPortName )
        {
            err_ = NvError_InsufficientMemory;
            goto clean;
        }
        err_ = NvOsCopyIn( pPortName, p_in->pPortName_data, p_in->pPortName_len );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
            goto clean;
        }
        if( pPortName[p_in->pPortName_len - 1] != 0 )
        {
            err_ = NvError_BadParameter;
            goto clean;
        }
    }
    if( p_in->RecvMessageSemaphore )
    {
        err_ = NvOsSemaphoreUnmarshal( p_in->RecvMessageSemaphore, &RecvMessageSemaphore );
        if( err_ != NvSuccess )
        {
            err_ = NvError_BadParameter;
            goto clean;
        }
    }

    p_out->ret_ = NvRmTransportOpen( p_in->hRmDevice, pPortName, RecvMessageSemaphore, &p_out->phTransport );

clean:
    if( pPortName != PortNameBuff )
        NvOsFree( pPortName );
    NvOsSemaphoreDestroy( RecvMessageSemaphore );
    return err_;
}

NvError nvrm_transport_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_transport_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvError err_ = NvSuccess;

    switch( function ) {
    case 10:
        err_ = NvRmTransportRecvMsg_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 9:
        err_ = NvRmTransportSendMsgInLP0_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 8:
        err_ = NvRmTransportSendMsg_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 7:
        err_ = NvRmTransportSetQueueDepth_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 6:
        err_ = NvRmTransportConnect_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 5:
        err_ = NvRmTransportWaitForConnect_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 4:
        err_ = NvRmTransportDeInit_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 3:
        err_ = NvRmTransportInit_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 2:
        err_ = NvRmTransportClose_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 1:
        err_ = NvRmTransportGetPortName_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    case 0:
        err_ = NvRmTransportOpen_dispatch_( InBuffer, InSize, OutBuffer, OutSize, Ctx );
        break;
    default:
        err_ = NvError_BadParameter;
        break;
    }

    return err_;
}
