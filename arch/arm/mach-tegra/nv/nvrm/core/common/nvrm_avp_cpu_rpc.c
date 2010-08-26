/*
 * arch/arm/mach-tegra/nvrm/core/common/nvrm_avp_cpu_rpc.c
 *
 * Transport API
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/** @file
 * @brief <b>NVIDIA Driver Development Kit:
 *          Transport API</b>
 *
 * @b Description: This is the wrapper implementation of Transport API.
 */

#include <linux/kernel.h>
#include <linux/string.h>

#include "nvrm_transport.h"
#include "nvrm_xpc.h"
#include "nvrm_rpc.h"
#include "nvrm_interrupt.h"
#include "nvassert.h"
#include "nvrm_graphics_private.h"

/* global variable passed from nvrpc_user.c */
extern NvRmTransportHandle g_hTransportAvp;
extern NvRmTransportHandle g_hTransportCpu;
extern NvOsSemaphoreHandle g_hTransportAvpSem;
extern NvOsSemaphoreHandle g_hTransportCpuSem;
extern int g_hTransportAvpIsConnected;
extern int g_hTransportCpuIsConnected;

/* local variables for handles */
static NvOsThreadHandle s_RecvThreadId_Service;
static NvRmRPCHandle gs_hRPCHandle = NULL;
volatile static int s_ContinueProcessing = 1;

#if !NV_IS_AVP
#define  PORT_NAME  "RPC_CPU_PORT"
#else
#define  PORT_NAME  "RPC_AVP_PORT"
#endif
/* Receive message port thread */
static void ServiceThread( void *args );
static void ServiceThread( void *args )
{
	NvError Error = NvSuccess;
	static NvU8 ReceiveMessage[MAX_MESSAGE_LENGTH];
	NvU32 MessageLength = 0;

	Error = NvRmPrivRPCWaitForConnect(gs_hRPCHandle);
	if (Error)
	{
		goto exit_gracefully;
	}
	while (s_ContinueProcessing)
	{
		Error = NvRmPrivRPCRecvMsg(gs_hRPCHandle, ReceiveMessage,
					&MessageLength);
		if (Error == NvError_InvalidState)
		{
			break;
		}
		if (!Error)
		{
			ReceiveMessage[MessageLength] = '\0';
		}
		NvRmPrivProcessMessage(gs_hRPCHandle, (char*)ReceiveMessage,
				MessageLength);
	}

exit_gracefully:
        return;
}

NvError NvRmPrivRPCInit(NvRmDeviceHandle hDeviceHandle, char* portName,
			NvRmRPCHandle *hRPCHandle )
{
	NvError Error = NvSuccess;

	*hRPCHandle = NvOsAlloc(sizeof(NvRmRPC));
	if (!*hRPCHandle)
	{
		Error = NvError_InsufficientMemory;
		return Error;
	}

	Error = NvOsMutexCreate(&(*hRPCHandle)->RecvLock);
	if( Error != NvSuccess)
	{
		goto clean_up;
	}

	if (! portName) {
		panic("%s: No port name.\n", __func__);
	}
	if (! strcmp(portName, "RPC_AVP_PORT")) {
		if (g_hTransportAvp) panic("%s: g_hTransportAvp is already set.\n", __func__);
		Error = NvOsSemaphoreCreate(&g_hTransportAvpSem, 0);
		if (Error != NvSuccess) panic(__func__);

		Error = NvRmTransportOpen(hDeviceHandle, portName, g_hTransportAvpSem,
					&g_hTransportAvp);
		if (Error != NvSuccess) panic(__func__);

		(*hRPCHandle)->svcTransportHandle = g_hTransportAvp;
		(*hRPCHandle)->TransportRecvSemId = g_hTransportAvpSem;
		(*hRPCHandle)->isConnected = g_hTransportAvpIsConnected;
	}
	if (! strcmp(portName, "RPC_CPU_PORT")) {
		if (g_hTransportCpu) panic("%s: g_hTransportCpu is already set.\n", __func__);
		Error = NvOsSemaphoreCreate(&g_hTransportCpuSem, 0);
		if (Error != NvSuccess) panic(__func__);

		Error = NvRmTransportOpen(hDeviceHandle, portName, g_hTransportCpuSem,
					&g_hTransportCpu);
		if (Error != NvSuccess) panic(__func__);

		(*hRPCHandle)->svcTransportHandle = g_hTransportCpu;
		(*hRPCHandle)->TransportRecvSemId = g_hTransportCpuSem;
		(*hRPCHandle)->isConnected = g_hTransportCpuIsConnected;
	}
	(*hRPCHandle)->hRmDevice = hDeviceHandle;

clean_up:
	return Error;
}

void NvRmPrivRPCDeInit( NvRmRPCHandle hRPCHandle )
{
	if(hRPCHandle != NULL)
	{
		if(hRPCHandle->svcTransportHandle != NULL)
		{
			NvOsSemaphoreDestroy(hRPCHandle->TransportRecvSemId);
			NvOsMutexDestroy(hRPCHandle->RecvLock);
			NvRmTransportClose(hRPCHandle->svcTransportHandle);
			hRPCHandle->svcTransportHandle = NULL;
			hRPCHandle->isConnected = NV_FALSE;
		}
		NvOsFree(hRPCHandle);
	}
}

void NvRmPrivRPCSendMsg(NvRmRPCHandle hRPCHandle,
			void* pMessageBuffer,
			NvU32 MessageSize)
{
	NvError Error = NvSuccess;
	NV_ASSERT(hRPCHandle->svcTransportHandle != NULL);

	NvOsMutexLock(hRPCHandle->RecvLock);
	Error = NvRmTransportSendMsg(hRPCHandle->svcTransportHandle,
				pMessageBuffer, MessageSize, NV_WAIT_INFINITE);
	NvOsMutexUnlock(hRPCHandle->RecvLock);
	if(Error)
		NV_ASSERT(Error == NvSuccess);
}

void NvRmPrivRPCSendMsgWithResponse( NvRmRPCHandle hRPCHandle,
				void* pRecvMessageBuffer,
				NvU32 MaxSize,
				NvU32 * pMessageSize,
				void* pSendMessageBuffer,
				NvU32 MessageSize)
{
	NvError Error = NvSuccess;
	NV_ASSERT(hRPCHandle->svcTransportHandle != NULL);

	NvOsMutexLock(hRPCHandle->RecvLock);
	Error = NvRmTransportSendMsg(hRPCHandle->svcTransportHandle,
				pSendMessageBuffer, MessageSize, NV_WAIT_INFINITE);
	if (Error)
	{
		// TODO: Determine cause of error and pass appropriate error to caller.
		NvOsDebugPrintf("%s: error in NvRmTransportSendMsg\n", __func__);
		goto clean_up;
	}
	NvOsSemaphoreWait(hRPCHandle->TransportRecvSemId);

	Error = NvRmTransportRecvMsg(hRPCHandle->svcTransportHandle,
				pRecvMessageBuffer, MaxSize, pMessageSize);
	if (Error)
	{
		NvOsDebugPrintf("%s: error in NvRmTransportRecvMsg\n", __func__);
		goto clean_up;
	}

clean_up:
	NV_ASSERT(Error == NvSuccess);
	NvOsMutexUnlock(hRPCHandle->RecvLock);
}

NvError NvRmPrivRPCWaitForConnect( NvRmRPCHandle hRPCHandle )
{
	NvError Error = NvSuccess;

	NV_ASSERT(hRPCHandle != NULL);
	NV_ASSERT(hRPCHandle->svcTransportHandle != NULL);

	if (hRPCHandle->isConnected) panic("%s: line=%d\n", __func__, __LINE__);
	if(hRPCHandle->isConnected == NV_FALSE)
	{
		Error = NvRmTransportSetQueueDepth(hRPCHandle->svcTransportHandle,
						MAX_QUEUE_DEPTH, MAX_MESSAGE_LENGTH);
		if (Error)
		{
			goto clean_up;
		}
		Error = NvError_InvalidState;
		// Connect to the other end
		while (s_ContinueProcessing)
		{
			Error = NvRmTransportWaitForConnect(
				hRPCHandle->svcTransportHandle, 100 );
			if (Error == NvSuccess)
			{
				hRPCHandle->isConnected = NV_TRUE;
				break;
			}
			// if there is some other issue than a timeout, then bail out.
			if (Error != NvError_Timeout)
			{
				goto clean_up;
			}
		}
	}

clean_up:
	return Error;
}

NvError NvRmPrivRPCConnect( NvRmRPCHandle hRPCHandle )
{
	NvError Error = NvSuccess;

	NV_ASSERT(hRPCHandle != NULL);
	NV_ASSERT(hRPCHandle->svcTransportHandle != NULL);

	/* if (hRPCHandle->isConnected) panic("%s: line=%d\n", __func__, __LINE__); */
	NvOsMutexLock(hRPCHandle->RecvLock);
	if(hRPCHandle->isConnected == NV_TRUE)
	{
		goto clean_up;
	}
	Error = NvRmTransportSetQueueDepth(hRPCHandle->svcTransportHandle,
					MAX_QUEUE_DEPTH, MAX_MESSAGE_LENGTH);
	if (Error)
	{
		goto clean_up;
	}
	Error = NvError_InvalidState;

#define CONNECTION_TIMEOUT (20 * 1000)

	// Connect to the other end with a large timeout
	// Timeout value has been increased to suit slow enviornments like
	// emulation FPGAs
	Error = NvRmTransportConnect(hRPCHandle->svcTransportHandle,
				CONNECTION_TIMEOUT );
	if(Error == NvSuccess)
	{
		hRPCHandle->isConnected = NV_TRUE;
	}
	else
	{
		NvOsDebugPrintf("%s: Not connected.\n", __func__);
	}

#undef CONNECTION_TIMEOUT

clean_up:
	NvOsMutexUnlock(hRPCHandle->RecvLock);
	return Error;
}

NvError NvRmPrivRPCRecvMsg( NvRmRPCHandle hRPCHandle, void* pMessageBuffer,
			NvU32 * pMessageSize )
{
	NvError Error = NvSuccess;
	NV_ASSERT(hRPCHandle->svcTransportHandle != NULL);

	if (s_ContinueProcessing == 0)
	{
		Error = NvError_InvalidState;
		goto clean_up;
	}

	NvOsSemaphoreWait(hRPCHandle->TransportRecvSemId);
	if(s_ContinueProcessing != 0)
	{

		Error = NvRmTransportRecvMsg(hRPCHandle->svcTransportHandle,
					pMessageBuffer, MAX_MESSAGE_LENGTH, pMessageSize);
	}
	else
	{
		Error = NvError_InvalidState;
	}
clean_up:
	return Error;
}

void NvRmPrivRPCClose( NvRmRPCHandle hRPCHandle )
{
	// signal the thread to exit
	s_ContinueProcessing = 0;
	if(hRPCHandle && hRPCHandle->svcTransportHandle != NULL)
	{
		if (hRPCHandle->TransportRecvSemId)
			NvOsSemaphoreSignal(hRPCHandle->TransportRecvSemId);
	}
}

NvError NvRmPrivInitService(NvRmDeviceHandle hDeviceHandle)
{
	NvError Error = NvSuccess;

	Error = NvRmPrivRPCInit(hDeviceHandle, PORT_NAME, &gs_hRPCHandle);
	if( Error != NvSuccess)
	{
		goto exit_gracefully;
	}
	NV_ASSERT(gs_hRPCHandle != NULL);

#if !NV_IS_AVP
	Error = NvOsInterruptPriorityThreadCreate(ServiceThread, NULL,
						&s_RecvThreadId_Service);
#else
	Error = NvOsThreadCreate(ServiceThread, NULL, &s_RecvThreadId_Service);
#endif

exit_gracefully:
	return Error;
}

void NvRmPrivServiceDeInit()
{
	NvRmPrivRPCClose(gs_hRPCHandle);
	NvOsThreadJoin(s_RecvThreadId_Service);
	NvRmPrivRPCDeInit(gs_hRPCHandle);
}
