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

#ifndef INCLUDED_nvrm_transport_H
#define INCLUDED_nvrm_transport_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_module.h"
#include "nvrm_init.h"

/** @file
 * @brief <b>NVIDIA Driver Development Kit:
 *     Resource Manager Transport APIs</b>
 *
 * @b Description: This is the Transport API, which defines a simple means to
 * pass messages across a lower level connection (generally between
 * processors).
 *
 */

/** @defgroup nvrm_transport RM Transport API
 * 
 * The Transport API defines a simple protocol through which clients and
 * services may connect and communicate--normally, though not necessarily,
 * across separate processors.  Clients to this interface mostly include
 * audio-visual applications whose code may reside on either the MPCore or AVP
 * processors.  These applications (and there could be many concurrently) may
 * utilize this transport API to synchronize their operations.  How the
 * Transport API shepherds messages through these connections is not visible to
 * the client.
 * 
 * To setup a new connection, both the client and the service must open a port
 * (whose name is agreed upon before compile-time).  The service waits for a
 * client to connect; this "handshake" allows a connection to be established.
 * Once a client has established a connection with the service, they may send
 * and receive messages.
 * 
 * @ingroup nvddk_rm
 * @{
 */

#include "nvos.h"

/**
 * A type-safe handle for the transport connection.
 */

typedef struct NvRmTransportRec *NvRmTransportHandle;

/**
 * Creates one end of a transport connection.  Both the service and client
 * to the service must call this API to create each endpoint of the connection
 * through a specified port (whose name is agreed upon before compile-time).
 * A connection is not established between the service and client until a 
 * handshake is completed (via calls to NvRmTransportWaitForConnect() and
 * NvRmTransportConnect() respectively).
 *
 * Assert in debug mode encountered if PortName is too long or does not exist 
 *
 * @see NvRmTransportWaitForConnect()
 * @see NvRmTransportConnect()
 * @see NvRmTransportClose()
 *
 * @param hRmDevice Handle to RM device
 * @param pPortName A character string that identifies the name of the port.
 * This value must be 16 bytes or less, otherwise the caller receives an error.
 * You can optionally pass NULL for this parameter, in which case a unique
 * name will  be assigned.  And you can call NvRmTransporGetPortName to retrieve
 * the name.
 * @param RecvMessageSemaphore The externally created semaphore that the
 * transport  connection will signal upon receipt of a message.
 * @param phTransport Points to the location where the transport handle shall
 * be stored
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

 NvError NvRmTransportOpen( 
    NvRmDeviceHandle hRmDevice,
    char * pPortName,
    NvOsSemaphoreHandle RecvMessageSemaphore,
    NvRmTransportHandle * phTransport );

/**
 * Retrieve the name associated with a port.
 *
 * Assert in debug mode encountered if PortName is too long or does not exist 
 *
 * @see NvRmTransportOpen()
 *
 * @param hTransport Handle to the port that you want the name of.
 * @param PortName A character string that identifies the name of the port.
 * @param PortNameSize Length of the PortName buffer.
 * 
 */

 void NvRmTransportGetPortName( 
    NvRmTransportHandle hTransport,
    NvU8 * PortName,
    NvU32 PortNameSize );

/**
 * Closes a transport connection.  Proper closure of this connection requires
 * that both the client and service call this API.  Therefore, it is expected
 * that the client and service message one another to coordinate the close.
 *
 * @see NvRmTransportOpen()
 * 
 * @param hTransport Specifies the transport connection to close.  If hTransport
 *     is NULL, this API does nothing.
 */

 void NvRmTransportClose( 
    NvRmTransportHandle hTransport );

/**
 * Initializes the transport.
 *
 * @param hRmDevice Handle to RM device
 *
 */

 NvError NvRmTransportInit( 
    NvRmDeviceHandle hRmDevice );

/**
 * Deinitializes the transport.
 *
 * @param hRmDevice Handle to RM device
 *
 */

 void NvRmTransportDeInit( 
    NvRmDeviceHandle hRmDevice );

/**
 * This handshake API is called by the service, which waits for a client to
 * establish a connection via a call to NvRmTransportConnect().  Messages 
 * cannot be sent and received until this handshake is completed.
 * 
 * To ensure a client has sufficient opportunity to establish a connection
 * from the other end, a large timeout value (such as NV_WAIT_INFINITE) is
 * recommended here.
 *
 * @see NvRmTransportConnect()
 *
 * @param hTransport Specifies the transport connection
 * @param TimeoutMS Specifies the amount of time (in milliseconds) to wait for
 *   connection to be established.  A value of NV_WAIT_INFINITE means "wait
 *   indefinitely."  A value of zero (0) will timeout immediately, which is
 *   not recommended for this function call.
 * 
 * @retval NvSuccess Service is waiting to receive a "connect" from client
 * @retval NvError_NotInitialized hTransport is not open
 * @retval NvError_Timeout Timed out waiting for service to respond
 */

 NvError NvRmTransportWaitForConnect( 
    NvRmTransportHandle hTransport,
    NvU32 TimeoutMS );

/**
 * This blocking handshake API is called by the client, which seeks a
 * service (as specified by a handle) to establish a connection.  Messages
 * cannot be sent and received until this handshake is completed.
 *
 * @see NvRmTransportWaitForConnect()
 *
 * @param hTransport Specifies the transport connection
 * @param TimeoutMS Specifies the amount of time (in milliseconds) to wait for
 *   connection to be established.  A value of NV_WAIT_INFINITE means "wait
 *   indefinitely."  A value of zero (0) will timeout immediately, but
 *   this function will at least take time to check if the port is open and
 *   waiting for a connection--if so, a connection will be established.
 * 
 * @retval NvSuccess Transport connection successfully established
 * @retval NvError_NotInitialized hTransport is not open
 * @retval NvError_Timeout Timed out waiting for service to respond.
 */

 NvError NvRmTransportConnect( 
    NvRmTransportHandle hTransport,
    NvU32 TimeoutMS );

/**
 * Set the max size of the message queue (FIFO) deptha nd length which can be 
 * send and receive from this port. The programmer must decide the
 * queue depth that's appropriate for their design.  If this function is not
 * called, the queue depth is set to one (1) and message size is 256 bytes.  
 * 
 *
 * @see NvRmTransportSendMsg()
 * @see NvRmTransportRecvMsg()
 *
 * @param hTransport Specifies the transport connection
 * @param MaxQueueDepth The maximum number of message which can be queued for 
 * this port for receiving and sending. The receive message can queue message 
 * till this count for this port. If receive queue is full for this port and
 * if other port send the message to this port then receive queue error status
 * will turn as overrun and ignore the incoming message.
 * If send message queue is full and client request to send message then he
 * will wait for time provided by the parameter.
 * @param MaxMessageSize Specifies the maximum size of the message in bytes
 * which client can receive and transmit through this port. 
 * 
 * @retval NvSuccess New queue depth is set
 * @retval NvError_NotInitialized hTransport is not open.
 * @retval NvError_BadValue The parameter passed is not correct. There is 
 * limitation for maximum message q and message length from the driver and if 
 * this parameter is larger than those value then it returns this error.
 *
 */

 NvError NvRmTransportSetQueueDepth( 
    NvRmTransportHandle hTransport,
    NvU32 MaxQueueDepth,
    NvU32 MaxMessageSize );

/**
 * Sends a message to the other port which is connected to this port.
 * This will use the copy method to copy the client buffer message to
 * transport message buffer. This function queue the message to the transmit
 * queue. the data will be send later based on the physical transfer channel
 * availablity.
 *
 * @see NvRmTransportOpen()
 * @see NvRmTransportSetQueueDepth()
 * @see NvRmTransportRecvMsg()
 *
 * @param hTransport Specifies the transport connection
 * @param pMessageBuffer The pointer to the message buffer where message which 
 * need to be send is available.
 * @param MessageSize Specifies the size of the message.
 * @param TimeoutMS Specifies the amount of time (in milliseconds) to wait for
 * sent message to be queued for the transfer. If the transmit queue if full
 * then this function will block the client till maximum of timeout to queue
 * this message. If meesage queue is available before timeout then it will
 * queue the message and comeout. If message queue is full and timeout happen
 * the it will return the timeout error.
 * if zero timeout is selecetd and the message queue is full then it will be
 * return NvError_TransportMessageBoxFull error.
 * Avalue of NV_WAIT_INFINITE means "wait indefinitely" for queueing the
 * message.
 * 
 * @retval NvSuccess Message is queued successfully.
 * @retval NvError_NotInitialized hTransport is not open.
 * @retval NvError_BadValue The parameter passed is not valid.
 * @retval NvError_InvalidState The port is not connected to the other port and
 * it is not ready for sending the message.
 * @retval NvError_Timeout Timed out waiting for message to be queue if send 
 * message queue.
 * @retval NvError_TransportMessageBoxFull Message box is full and it is not
 * able to queue the message.
 */

 NvError NvRmTransportSendMsg( 
    NvRmTransportHandle hTransport,
    void* pMessageBuffer,
    NvU32 MessageSize,
    NvU32 TimeoutMS );

/**
 * Sends a message to the other port which is connected to this port.
 * This function is to be used ONLY when we're about to enter LP0!
 * There is no synchronization in this function as only one person
 * should be talking to the AVP at the time of LP0. The message is sent
 * on the RPC_AVP_PORT. In the future, there might be instances where
 * we need to talk on a different port in LP0. 
 *
 * @retval NvSuccess Message is queued successfully.
 * @retval NvError_TransportMessageBoxFull Message box is full and it is not
 * able to queue the message.
 */

 NvError NvRmTransportSendMsgInLP0( 
    NvRmTransportHandle hPort,
    void* message,
    NvU32 MessageSize );

/**
 * Receive the message from the port. This will read the message if it is 
 * available for this port otherwise it will return the
 * NvError_TransportMessageBoxEmpty error.
 *
 * @see NvRmTransportOpen()
 * @see NvRmTransportSetQueueDepth()
 * @see NvRmTransportSendMsg()
 *
 * @param hTransport Specifies the transport connection
 * @param pMessageBuffer The pointer to the receive message buffer where the
 * received message will be copied.
 * @param MaxSize The maximum size in bytes that may be copied to the buffer
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

 NvError NvRmTransportRecvMsg( 
    NvRmTransportHandle hTransport,
    void* pMessageBuffer,
    NvU32 MaxSize,
    NvU32 * pMessageSize );

#if defined(__cplusplus)
}
#endif

#endif
