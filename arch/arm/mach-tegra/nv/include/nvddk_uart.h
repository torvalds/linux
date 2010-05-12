/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
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

/**
 * @file
 * <b>NVIDIA Driver Development Kit: UART Driver Interface</b>
 *
 * @b Description: This file defines the interface to the UART driver.
 */

#ifndef INCLUDED_NVDDK_UART_H
#define INCLUDED_NVDDK_UART_H

/**
 * @defgroup nvddk_uart UART Driver Interface
 * 
 * This is the Universal Asynchronous Receiver Transmitter (UART) interface.
 * There may be more than one UART in the SOC, which communicate with other 
 * systems. This interface provides the communication channel configuration,
 * basic data transfer (receive and transmit) and hardware flow control (modem
 * flow control).
 * This driver does not support any software protocols, like IrDA SIR protocol.
 * 
 * @ingroup nvddk_modules
 * @{
 */

#include "nvcommon.h"
#include "nvos.h"
#include "nvrm_init.h"
#include "nvrm_module.h"

#if defined(__cplusplus)
extern "C"
{
#endif

/** Opaque context to the NvDdkUartRec interface.
 */
typedef struct NvDdkUartRec *NvDdkUartHandle;


/**
 * Defines the UART communication signal configuration for parity bit.
 */
typedef enum 
{
    /// Specifies parity to be none.
    NvDdkUartParity_None = 0x1,
    /// Specifies parity to be odd.
    NvDdkUartParity_Odd,
    /// Specifies even parity to be even.
    NvDdkUartParity_Even,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvDdkUartParity_Force32 = 0x7FFFFFFF
} NvDdkUartParity;

/**
 * Defines the UART communication signal configuration for stop bit.
 */
typedef enum 
{
    /// Specifies stop bit 1, word length can be 5, 6, 7, or 8.
    NvDdkUartStopBit_1= 0x1,
    /// Specifies stop bit 2, word length can be 6, 7, or 8.
    NvDdkUartStopBit_2,
    /// Specifies stop bit 1.5, word length should be 5 only.
    NvDdkUartStopBit_1_5,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvDdkUartStopBit_Force32 = 0x7FFFFFFF
} NvDdkUartStopBit;

/**
 * Defines the UART modem signal name to get/set the status/value.
 */
typedef enum 
{
    /// Specifies a modem signal name of RxD.
    NvDdkUartSignalName_Rxd = 0x1,
    /// Specifies a modem signal name of TxD.
    NvDdkUartSignalName_Txd = 0x2,
    /// Specifies a modem signal name of RTS.
    NvDdkUartSignalName_Rts = 0x4,
    /// Specifies a modem signal name of CTS.
    NvDdkUartSignalName_Cts = 0x8,
    /// Specifies a modem signal name of DTR.
    NvDdkUartSignalName_Dtr = 0x10,
    /// Specifies a modem signal name of DSR.
    NvDdkUartSignalName_Dsr = 0x20,
    /// Specifies a modem signal name for ring indicator.
    NvDdkUartSignalName_Ri = 0x40,
    /// Specifies a modem signal name for carrier detect.
    NvDdkUartSignalName_Cd = 0x80,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvDdkUartSignalName_Force32 = 0x7FFFFFFF
} NvDdkUartSignalName;

/**
 * Defines the HW flow control signal states and their behavior.
 * This is applicable for the modem flow control signal, like RTS, CTS, DSR, DTR,
 * RI, and CD.
 * The handshake flow control is configured for the RTS and CTS. When RTS and CTS 
 * lines are set for the handshake the driver will transfer the data based on 
 * status of the line.
 */
typedef enum
{
    /// Disable the flow control. The output signal state will be low.
    NvDdkUartFlowControl_Disable = 0x1,
    
    /// Enable the flow control. The output signal state will be high.
    NvDdkUartFlowControl_Enable,
    
    /// Enable the handshake of the flow control line. 
    /// This is applicable for the RTS and CTS line. 
    /// For RTS line, when the buffer is full or UART driver is not able to 
    /// receive the data, it will deactivate the line.
    /// For CTS line, the data is transmitted only when the CTS line is active,
    /// otherwise it will not send the data.
    NvDdkUartFlowControl_Handshake,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvDdkUartFlowControl_Force32 = 0x7FFFFFFF    
} NvDdkUartFlowControl;

 /**
 * Combines the UART port configuration parameter, like baud rate,
 * parity, data length, stop bit, IrDA modulation, and interfacing type.
 */
typedef struct
{
    /// Holds the baud rate. Baudrate should be in the bps (bit per second).
    NvU32 UartBaudRate;

    /// Holds the parity bit. This can be even, odd, or none.
    NvDdkUartParity UartParityBit;
    
    /// Holds the data length in number of bits per UART asynchronous frame.
    /// This is number of bits between start and stop bit of UART asynch frame.
    /// The valid length are 5,6,7, and 8.
    NvU8 UartDataLength;
    
    /// Holds the stop bit.
    /// The UART controller does not support all stop bits with all data
    /// lengths (16550 compatible UART). 
    /// The valid combinations are:
    /// 1 stop bit for data length 5, 6, 7, or 8.
    /// 1.5 stop bit for data length 5.
    /// 2 stop bit for data length 6, 7, or 8.
    NvDdkUartStopBit UartStopBit;
    
    /// Holds whether IrDA signal modulation is enabled or not.
    NvBool IsEnableIrdaModulation;
} NvDdkUartConfiguarations;

/**
 * Opens the UART channel and creates the handle of UART. This function 
 * allocates the memory/OS resources for the requested UART channel and 
 * returns the handle to the client. The client will call other API by 
 * passing this handle. 
 * This initializes the UART controller.
 *
 * @param hDevice Handle to the Rm device that is required by DDK to acquire 
 * the resources from RM.
  * @param ChannelId Specifies the UART channel ID for which context handle is 
 * required. Valid instance ID start from 0.
 * @param phUart A pointer to the UART handle where the allocated handle pointer
 * will be stored.
 *
 * @retval NvSuccess Indicates the controller successfully initialized.
 * @retval NvError_InsufficientMemory Indicates that function fails to allocate
 * the memory for handle.
 * @retval NvError_AlreadyOpen Indicates a channel is already open and so it 
 * returns the NULL handle.
 * @retval NvError_BadValue Indicates that the channel ID is not valid. It may
 * be more than supported channel ID.
 * @retval NvError_MemoryMapFailed Indicates that the memory mapping for 
 * controller register failed.
 * @retval NvError_MutexCreateFailed Indicates that the creation of mutex
 * failed. Mutex is required to provide the thread safety.
 * @retval NvError_SemaphoreCreateFailed Indicates that the creation of
 * semaphore failed. Semaphore is required to provide the synchronous
 * operation.
 */
NvError 
NvDdkUartOpen(
    NvRmDeviceHandle hDevice,
    NvU32 ChannelId, 
    NvDdkUartHandle *phUart);

/**
 * Deinitialize the UART controller and release the UART handle. This
 * frees the memory/OS resources which is allocated for the UART driver related
 * to this channel ID. After calling this API by client, client should not call
 * any other APIs related to this handle.
 *
 * @param hUart Handle to the UART which is allocated from Open().
 */
void NvDdkUartClose(NvDdkUartHandle hUart);


/**
 *  Sets the different UART port configuration. It will set the
 * baud rate, parity bit, data length, stop bit, line interfacing type, Irda
 * modulation, flow control. It returns the related error if any of the
 * parameter is out of range or not supported.
 *
 * The baud rate should be less than the supported maximum baudrate.
 * The UART controller does not support all stop bits  with all data lengths
 * (16550 compatible UART). The valid combinations are:
 * - 1 stop bit for data length 5, 6, 7, or 8
 * - 1.5 stop bit for data length 5
 * - 2 stop bit for data length 6, 7, or 8
 *
 * @note It is recommended that client first call the
 * NvDdkUartGetConfiguration() to get the current setting and change only those
 * parameters that are required to change. Do not touch the other parameters and
 * then call this function.
 *
 * @param hUart Handle to the UART.
 * @param pUartDriverConfiguration A pointer to the structure where the settings 
 * are stored.
 * 
 * @retval NvSuccess Indicates the operation succeeded.
 * @retval NvError_NotInitialized Indicates that the UART channel is not opened.
 * @retval NvError_BadValue Indicates that illegal value specified for the
 * parameter.
 * The possible cases for this error are:
 * - The data length is illegal, e.g. it is not 5,6,7, or 8.
 * - The stop bit restriction is not matching with the data length.
 * - The baud rate is more than maximum supported baud rate.
 * @retval NvError_NotSupported There may be many case to return this error. 
 * Possible cases are:
 * - Requested baudrate is not supported because of the it may be not 
 *          possible to set the correct timing related to this  baud rate.
 * -  The requested parity bit is not supported.
 */
NvError 
NvDdkUartSetConfiguration(
    NvDdkUartHandle hUart, 
    const NvDdkUartConfiguarations* const pUartDriverConfiguration);

/**
 *  Gets the UART port configuration parameter which is configured.
 * Client can get the configuration parameter after calling this function.
 *
 * @note If client wants to set any port parameter, it is better to first call
 * this function for getting the default value and then change the desired
 * parameter with new value and call the NvDdkUartSetConfiguration().
 *
 * @param hUart Handle to the UART.
 * @param pUartDriverConfiguration A pointer to the structure where the
 * information will be stored.
 * 
 * @retval NvSuccess Indicates the operation succeeded.
 * @retval NvError_NotInitialized Indicates that the UART channel is not opened.
 * @retval NvError_BadValue Indicates that illegal value specified for the
 * parameter.
 */
NvError 
NvDdkUartGetConfiguration(
    NvDdkUartHandle hUart, 
    NvDdkUartConfiguarations* const pUartDriverConfiguration);

/**
 *  Start the read opeartion from the HW and store the receive data in 
 * the local buffer created locally at the driver level with the buffer size.
 * This function will create the local buffer for the receive
 * data as requested by client. The receive data will be stored in this local 
 * buffer if there is no read call from client side and data arrived. When 
 * client makes the read call, it will first copied the data from the local buffer 
 * to the requested buffer and then it will wait for reading the remaining 
 * data (if requested number of bytes was not available on the local buffer).  
 *
 * It will also signal the semaphore \a hRxEventSema if the number of bytes 
 * available in the local buffer changes from 0 to any value and if there is no
 * read call. Means if there is no read call and there is no data available on 
 * the local buffer and when data arrives, the data will be copied into the 
 * local buffer and it signals the semaphore. This will be use by the client 
 * that data are available in the local buffer, and so client can made the read 
 * call.
 *
 * It also notifies to the client by signalling the semaphore \a hRxEventSema
 * if there is any error or break condition received in the receive line.
 *
 * @param hUart Handle to the UART.
 * @param hRxEventSema The semaphore ID which is signalled if any data is 
 * recevived or there is any error in the receive flow.
 * @param BufferSize Size of the local buffer where received data will be 
 * buffered.
 *
 * @retval NvSuccess Indicates the operation succeeded.
 * @retval NvError_NotInitialized Indicates that the UART channel is not opened.
 * @retval NvError_BadValue Indicates that illegal value specified for the local
 * \a BufferSize.
 * @retval NvError_InsufficientMemory Indicates that it is not able to create
 * the memory for requested size.
 */

NvError
NvDdkUartStartReadOnBuffer(
    NvDdkUartHandle hUart,
    NvOsSemaphoreHandle hRxEventSema,
    NvU32 BufferSize);
    
/**
 *  Clears the receive buffer. All data will be cleared and the 
 * counter which keeps the number of bytes available will be reset to 0.
 *
 * @param hUart Handle to the UART.
 *
 * @retval NvSuccess Indicates the operation succeeded.
 * @retval NvError_NotInitialized Indicates that the UART channel is not opened.
 * @retval NvError_NotSupported Indicates that this feature is not supported.
 */
NvError NvDdkUartClearReceiveBuffer(NvDdkUartHandle hUart);

/**
 *  Update the local buffer if the data arrives in the UART.
 * This API reads the data from HW FIFO to the local buffer once the data has
 * arrived. This also returns the number of bytes available in the FIFO.
 * This API should be called once the client gets the notification from the DDK.
 *
 * @param hUart Handle to the UART.
 * @param pAvailableBytes Returns the number of bytes available in the local
 * buffer.
 *
 * @retval NvSuccess Indicates the operation succeeded.
 * @retval NvError_NotInitialized Indicates that the UART channel is not opened.
 * @retval NvError_NotSupported Indicates that this feature is not supported.
 */
NvError 
NvDdkUartUpdateReceiveBuffer(
    NvDdkUartHandle hUart, 
    NvU32 *pAvailableBytes);

/**
 *  Starts the data receiving with the buffer provided. This is blocking
 * type.
 *
 * First it copies the available data from the local buffer to the client
 * buffer, and if bytes are remaining to read then:
 * - It will wait for reading the remaining data (synchronous ops), or 
 * - keep reading from the UART channel to the client buffer and signal
 *    when there is no remaining data (async ops) or 
 * - no more reading of the remaining data in the client buffer (read only
 *         from local buffer).
 *
 * If non-zero timeout is selected then it will wait maximum for a given
 * timeout for reading the data from channel. It can also wait for forever
 * based on the argument passed. 
 * If zero timeout is selected then it just copies from local buffer to the 
 * client buffer with available number of bytes (if it is less than the 
 * requested size) or requested number of bytes (if available data is more 
 * than the requested size) and immediately return. 
 *
 * @note If previous read is going on then this read call will return an error.
 *
 * @param hUart Handle to the UART.
 * @param pReceiveBuffer A pointer to the receive buffer where data
 * will be stored.
 * @param BytesRequested Number of bytes need to be read.
 * @param pBytesRead A pointer to the variable that stores the number of bytes
 * requested to read when it is called and stores the actual number of bytes
 * read when return from the function.
 * @param WaitTimeoutMs The time needed to wait in milliseconds. If
 * it is zero then it will be returned immediately with reading the 
 * number of bytes available in local buffer.
 * If is non-zero, then it will wait for a requested timeout. If it is
 * ::NV_WAIT_INFINITE then it will wait for infinitely until the transaction
 * completes.
 *
 * @retval NvSuccess Indicates the operation succeeded.
 * @retval NvError_NotInitialized Indicates that the UART channel is not opened.
 * @retval NvError_Timeout Indicates the operation is not completed in a given
 * timeout.
 * @retval NvError_UartOverrun Indicates that overrun error occur during
 * receiving of the data.
 * @retval NvError_UartFifo Indicates the operation is not completed because of
 * FIFO error.
 * @retval NvError_UartBreakReceived Indicates the break condition received.
 * @retval NvError_UartFraming Indicates the operation is not completed due to 
 * framing error.
 * @retval NvError_UartParity Indicates the operation is  not completed due to
 * parity error.
 * @retval NvError_InvalidState Indicates that the last read call is not
 * completed/stopped. 
 */
NvError
NvDdkUartRead(
    NvDdkUartHandle hUart,
    NvU8 *pReceiveBuffer,
    NvU32 BytesRequested,
    NvU32 *pBytesRead,
    NvU32 WaitTimeoutMs);

/**
 * Stops the read operation. The NvDdkUartRead() will be aborted.
 * The DDK will keep reading the data from the external interface to the local 
 * buffer and it will not be cleared.
 *
 * @param hUart Handle to the UART.
 *
 */
void NvDdkUartStopRead( NvDdkUartHandle hUart); 


/**
 *  Starts the data transfer with the buffer provided. This is blocking
 * type call. If zero timeout is selected then it will return immediately 
 * without transferring any data.
 * 
 * @param hUart Handle to the UART.
 * @param pTransmitBuffer A pointer to the transmit buffer where transmitted
 * data are available.
 * @param BytesRequested Number of bytes to be sent.
 * @param pBytesWritten A pointer to the variable that stores the number of
 * bytes requested to transmit when it is called and stores the actual number of
 * bytes transmitted when returning from the function.
 * @param WaitTimeoutMs The time need to wait in milliseconds. If
 * it is zero then it will be returned immediately without sending any data.
 *
 * @retval NvSuccess Indicates the operation succeeded.
 * @retval NvError_NotInitialized Indicates that the UART channel is not opened.
 * @retval NvError_Timeout Indicates the operation is not completed in a given
 * timeout.
 * @retval NvError_UartTransmit Indicates that a transmit error happened during 
 * sending of the data.
 * @retval NvError_InvalidState Indicates that there is already write call made
 * that is not completed yet.
 */
NvError 
NvDdkUartWrite(
    NvDdkUartHandle hUart,   
    NvU8 *pTransmitBuffer,
    NvU32 BytesRequested,
    NvU32 *pBytesWritten,
    NvU32 WaitTimeoutMs);

/**
 *  Stops the write operation. No more data will be transmitted from the 
 * buffer, which was passed with the function NvDdkUartWrite().
 *
 * @param hUart Handle to the UART provided after getting the channel.
 */
void NvDdkUartStopWrite( NvDdkUartHandle hUart); 

/**
 *  Gets the current transfer status at the UART channel. This API
 * returns the number of bytes remaining to send on the channel, transmit status,
 * number of bytes available in the rx buffer, and receive status.
 * This API returns the status at the calling time and after calling this API,
 * the data may be changed as data transfer may be still going on.
 * This is just polling type query about the data transfer status.
 *
 * @param hUart Handle to the UART provided after getting the channel.
 * @param pTxBytesToRemain A pointer to variable where number of bytes remaining
 * to transfer is stored.
 * @param pTxStatus A pointer to variable where tramsit status is stored.
 * @param pRxBytesAvailable A pointer to variable where number of bytes available 
 * in rx buffer is returned.
 * @param pRxStatus A pointer to variable where receive status is returned.
 *
 */
void 
NvDdkUartGetTransferStatus(
    NvDdkUartHandle hUart,
    NvU32 *pTxBytesToRemain,
    NvError *pTxStatus,
    NvU32 *pRxBytesAvailable,
    NvError *pRxStatus);


/**
 * Starts/stops sending the break signal from the channel. 
 * The break siganl can be started by calling this function with \a IsStart = NV_TRUE,
 * and it can be stopped by calling this API with \a isStart = NV_FALSE.
 *
 * @param hUart Handle to the UART.
 * @param IsStart NV_TRUE to start sending the break signal, or NV_FALSE to stop.
 *
 * @retval NvSuccess Indicates the operation succeeded.
 * @retval NvError_NotInitialized Indicates that the UART channel is not opened.
 * @retval NvError_NotSupported Indicates that this feature is not supported.
 */
NvError NvDdkUartSetBreakSignal(NvDdkUartHandle hUart, NvBool IsStart);

/**
 * Sets the flow control signal to be disabled, enabled, or in handshake mode.
 *
 * @param hUart Handle to the UART.
 * @param SignalName Specifies the name of the signal to set.
 * @param FlowControl Specifies whether this is disabled, enable, or in handshake 
 * mode.
 *
 * @retval NvSuccess Indicates the operation succeeded.
 * @retval NvError_NotInitialized Indicates that the UART channel is not opened.
 * @retval NvError_NotSupported Indicates that requested functionality is not 
 * supported for given signal.
 * @retval NvError_BadValue Indicates that illegal value specified for the
 * parameter. This may be because the signal name is not valid for this operation.
 */
NvError 
NvDdkUartSetFlowControlSignal(
    NvDdkUartHandle hUart, 
    NvDdkUartSignalName SignalName, 
    NvDdkUartFlowControl FlowControl);

/**
 * Gets the flow control signal level. This will tell the actual level of
 * the signal on the UART pins.
 * This API can be called by more than one signal name by ORing them.
 * The state of the signal (high or low) can be determined by the position of the
 * bit state.
 *
 * @param hUart Handle to the UART.
 * @param SignalName Specifies the name of the signal whose status need to be 
 * queried. Can be more than one signal name by ORing them.
 * @param pSignalState The state of the signal. The 1 in corresponding location
 * shows that state is high, otherwise it shows as low.
 *
 * @retval NvSuccess Indicates the operation succeeded.
 * @retval NvError_NotInitialized Indicates that the UART channel is not opened.
 * @retval NvError_NotSupported Indicates that requested functionality is not 
 * supported for given signal.
 * @retval NvError_BadValue Indicates an illegal value was specified for the
 * parameter. This may be because the signal name is not valid for this operation.
 */
NvError 
NvDdkUartGetFlowControlSignalLevel(
    NvDdkUartHandle hUart, 
    NvDdkUartSignalName SignalName,
    NvU32 *pSignalState);

typedef void (*NvDdkUartSignalChangeCallback)(void *args);

/**
 * Registers a callback funciton for the modem signal state change. 
 * Whenever the modem signal change, this API is called.
 * Callback typically will call NvDdkUartGetFlowControlSignalLevel() for 
 * finding the signal status.
 *
 * Clients can pass NULL callback function for unregistering the signal
 * change.
 *
 * The callback function is called from the ISR/IST.
 *
 * @param hUart Handle to the UART.
 * @param SignalName Specifies the name of the signal to observe.
 * @param Callback Callback function which is called from ISR/IST of DDK whenever 
 * signal change is detected by the DDK.
 * @param args Argument to the signal change handler.
 *
 * @retval NvSuccess Indicates the operation succeeded.
 * @retval NvError_NotInitialized Indicates that the UART channel is not opened.
 * @retval NvError_NotSupported Indicates that requested functionality is not 
 * supported.
 * @retval NvError_BadValue Indicates that an illegal value was specified for the
 * parameter. 
 */
NvError 
NvDdkUartRegisterModemSignalChange(
    NvDdkUartHandle hUart, 
    NvDdkUartSignalName SignalName,
    NvDdkUartSignalChangeCallback Callback,
    void *args);

/**
 * Power mode suspend the UART controller. 
 *
 * @param hUart Handle to the UART.
 *
 * @retval NvSuccess Indicates the operation succeeded.
 * @retval NvError_NotSupported Indicates that requested functionality is not 
 * supported.
 */
NvError NvDdkUartSuspend(NvDdkUartHandle hUart);

/**
 * Power mode resume the UART controller. This will resume the controller
 * from the suspend states.
 *
 * @param hUart Handle to the UART.
 *
 * @retval NvSuccess Indicates the operation succeeded.
 * @retval NvError_NotSupported Indicates that requested functionality is not 
 * supported.
 */
NvError NvDdkUartResume(NvDdkUartHandle hUart);


/** @} */


#if defined(__cplusplus)
}
#endif

#endif // INCLUDED_NVDDK_UART_H 
