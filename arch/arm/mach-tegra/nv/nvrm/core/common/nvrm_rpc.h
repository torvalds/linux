/*
 * arch/arm/mach-tegra/nvrm/core/common/nvrm_rpc.h
 *
 * communication between processors (cpu and avp)
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

#ifndef NVRM_RPC_H
#define NVRM_RPC_H

/*
 * nvrm_cpu_avp_rpc_private.h defines the private implementation functions to facilitate
 * communication between processors (cpu and avp).
 */

#include "nvcommon.h"
#include "nvos.h"
#include "nvrm_init.h"
#include "nvrm_message.h"

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */


/**
 * Initialize RPC
 *
 * Init the RPC.  Both the service and client
 * to the service must call this API before calling to create each endpoint of the connection
 * via NvRmPrivRPCConnect
 *
 * If PortName is too long or does not exist debug mode
 * assert is encountered.
 *
 * @param hDeviceHandle rm device handle
 * @param rpcPortName the port name
 * @param hRPCHandle the RPC transport handle
 *
 * @retval NvError_SemaphoreCreateFailed Creaion of semaphore failed.
 */
 NvError NvRmPrivRPCInit( NvRmDeviceHandle hDeviceHandle, char* rpcPortName, NvRmRPCHandle *hRPCHandle );
/**
 * De-intialize the RPC and other resources.
 * @param hRPCHandle the RPC transport handle
 *
 */
void NvRmPrivRPCDeInit( NvRmRPCHandle hRPCHandle );

/**
 * Connect to RPC port
 *
 * Creates one end of a RPC connection.  Both the service and client
 * to the service must call this API to create each endpoint of the connection
 * through a specified port
 *
 * If PortName is too long or does not exist debug mode
 * assert is encountered.
 *
 * @param hRPCHandle the RPC transport handle
 *
 * @retval NvSuccess Transport endpoint successfully allocated
 * @retval NvError_InsufficientMemory Not enough memory to allocate endpoint
 * @retval NvError_MutexCreateFailed Creaion of mutex failed.
 * @retval NvError_SemaphoreCreateFailed Creaion of semaphore failed.
 * @retval NvError_SharedMemAllocFailed Creaion of shared memory allocation
 * failed.
 * @retval NvError_NotInitialized The transport is not able to initialzed the
 * threads.
 */
 NvError NvRmPrivRPCConnect( NvRmRPCHandle hRPCHandle );

 /**
 * Connect to RPC port
 *
 * Creates one end of a RPC connection.  Both the service and client
 * to the service must call this API to create each endpoint of the connection
 * through a specified port
 *
 * If PortName is too long or does not exist debug mode
 * assert is encountered.
 *
 * @param hRPCHandle the RPC transport handle
 *
 * @retval NvSuccess Transport endpoint successfully allocated
 * @retval NvError_InsufficientMemory Not enough memory to allocate endpoint
 * @retval NvError_MutexCreateFailed Creaion of mutex failed.
 * @retval NvError_SemaphoreCreateFailed Creaion of semaphore failed.
 * @retval NvError_SharedMemAllocFailed Creaion of shared memory allocation
 * failed.
 * @retval NvError_NotInitialized The transport is not able to initialzed the
 * threads.
 */
 NvError NvRmPrivRPCWaitForConnect( NvRmRPCHandle hRPCHandle );
 /**
 * Receive the message from the port. This will read the message if it is
 * available for this port otherwise it will return the
 * NvError_TransportMessageBoxEmpty error.
 *
 * @param hRPCHandle the RPC transport handle
 * @param pMessageBuffer The pointer to the receive message buffer where the
 * received message will be copied.
 * @param pMessageSize Pointer to the variable where the length of the message
 * will be stored.
 *
 * @retval NvSuccess Message received successfully.
 * @retval NvError_NotInitialized hTransport is not open.
 * @retval NvError_InvalidState The port is not connection state.
 * @retval NvError_TransportMessageBoxEmpty The message box empty and not able
 * to receive the message.
 * @retval NvError_TransportIncompleteMessage The received message for this
 * port is longer than the configured message length for this port. It copied
 * the maximm size of the configured length of the message for this port and
 * return the incomplete message buffer.
 * @retval NvError_TransportMessageOverflow The port receives the message more
 * than the configured queue depth count for this port and hence message
 * overflow has been ocuured.
 */

 NvError NvRmPrivRPCRecvMsg( NvRmRPCHandle hRPCHandle, void* pMessageBuffer, NvU32 * pMessageSize );

 /**
 * Send Message.
 *
 * Sends a message to the other port which is connected to this port.
 * Its a wrapper to rm transport send message
 *
 * @param hRPCHandle the RPC transport handle
 * @param pMessageBuffer The pointer to the message buffer where message which
 * need to be send is available.
 * @param MessageSize Specifies the size of the message.
 *
 */
void
NvRmPrivRPCSendMsg(NvRmRPCHandle hRPCHandle,
                   void* pMessageBuffer,
                   NvU32 MessageSize);

/**
 * Send and Recieve message.
 *
 * Send and Recieve a message between port.
 * Its a wrapper to rm transport send message with response
 *
 * @param hRPCHandle the RPC transport handle
 * @param pRecvMessageBuffer The pointer to the receive message buffer where the
 * received message will be copied.
 * @param MaxSize The maximum size in bytes that may be copied to the buffer
 * @param pMessageSize Pointer to the variable where the length of the message
 * will be stored.
 * @param pSendMessageBuffer The pointer to the message buffer where message which
 * need to be send is available.
 * @param MessageSize Specifies the size of the message.
 *
 */
void
NvRmPrivRPCSendMsgWithResponse(NvRmRPCHandle hRPCHandle,
                               void* pRecvMessageBuffer,
                               NvU32 MaxSize,
                               NvU32 *pMessageSize,
                               void* pSendMessageBuffer,
                               NvU32 MessageSize);


/**
 * Closes a transport connection.  Proper closure of this connection requires
 * that both the client and service call this API.  Therefore, it is expected
 * that the client and service message one another to coordinate the close.
 *
 */
void NvRmPrivRPCClose(NvRmRPCHandle hRPCHandle);

NvError  NvRmPrivInitService(NvRmDeviceHandle hDeviceHandle);

void NvRmPrivServiceDeInit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */


#endif
