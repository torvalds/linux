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

#ifndef INCLUDED_nvrm_spi_H
#define INCLUDED_nvrm_spi_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_pinmux.h"
#include "nvrm_module.h"
#include "nvrm_init.h"

#include "nvcommon.h"

/**
 * NvRmSpiHandle is an opaque context to the NvRmSpiRec interface.
 */

typedef struct NvRmSpiRec *NvRmSpiHandle;

/**
 * Open the handle for the spi/sflash controller. This api initalise the 
 * sflash/spi controller.
 * The Instance Id for the sflash and spi controller start from 0.
 * The handle for the spi/sflash is open in master and slave mode based on the
 * parameter passed. If the spi handle is opened in master mode the the SPICLK
 * is generated from the spi controller and it acts like a master for all the 
 * transaction.
 *
 * If the spi handle is opened in master mode then the controller can be shared 
 * between different chip select client but if the spi handle is created in the 
 * slave mode then it can not be shared by other client and only one client is
 * allowed to open the spi handle for the slave mode.
 *
 * Assert encountered in debug mode if invalid parameter passed.
 *
 * @param hRmDevice Handle to the Rm device.
 * @param IoModule The Rm IO module to set whether this is the 
 * NvOdmIoModule_Sflash or NvOdmIoModule_Slink or NvOdmIoModule_Spi.
 * @param InstanceId  The Instance Id which starts from the 0.
 * @param IsMasterMode  Tells whether the controller will be open in master mode
 * or the slave mode?
 * @param phRmSpi  Pointer to the sflash/spi handle where the allocated handle 
 * will be stored.
 *
 * @retval NvSuccess Indicates the function is successfully completed
 * @retval NvError_MemoryMappingFail Indicates the address mapping of the
 * register failed.
 * @retval NvError_InsufficientMemory Indicates that memory allocation is
 * failed.
 * @retval NvError_NotSupported Indicases that the spi is not supported.
 * @retval NvError_AlreadyAllocated Indicases that the spi handle is already 
 * allocated to the other slave client.
 */

 NvError NvRmSpiOpen( 
    NvRmDeviceHandle hRmDevice,
    NvU32 IoModule,
    NvU32 InstanceId,
    NvBool IsMasterMode,
    NvRmSpiHandle * phRmSpi );

/**
 * Deinitialize the spi controller, disable the clock and release the spi
 * handle.
 *
 * @param hRmSpi A handle from NvRmSpiOpen().  If hRmSpi is NULL, this API does
 *     nothing.
 */

 void NvRmSpiClose( 
    NvRmSpiHandle hRmSpi );

/**
 * Performs an Spi controller read and write simultaneously in master mode. 
 * This apis is only supported if the handle is open in master mode.
 *
 * Every Spi transaction is by definition a simultaneous read and write transaction, so 
 * there are no separate APIs for read versus write. However, if you only need 
 * to do a read or write, this API allows you to declare that you are not 
 * interested in the read data, or that the write data is not of interest.
 * If only read is required then client can pass the NULL pointer to the 
 * pWriteBuffer. Zeros will be sent in this case.
 * Similarly, if client wants to send data only then he can pass the 
 * pReadBuffer as NULL.
 * If Read and write is required and he wants to first send the command and 
 * then want to read the response, then he need to send both the valid pointer
 * read and write. In this case the bytesRequested will be the sum of the
 * send command size and response size. The size of the pReadBuffer and 
 * pWriteBuffer should be equal to the bytes requetsed.
 * E.g. Client want to send the 4byte command first and the wants to read the
 * 4 byte response, then he need a 8 byte pWriteBuffer and 8 byte pReadBuffer.
 * He will fill the first 4 byte of pWriteBuffer with the command which he
 * wants to send. After calling this api, he needs to ignore the first 4 bytes
 * and use the next 4 byte as valid response data in the pReadBuffer.
 *
 * This is a blocking API. It will returns when all the data has been transferred
 * over the pins of the SOC (the transaction).
 *
 * Several Spi transactions may be performed in a single call to this API, but
 * only if all of the transactions are to the same chip select and have the same
 * packet size.
 *
 * Transaction sizes from 1 to 32 bits are supported. However, all of the 
 * packets are byte-aligned in memory. Like, if packetBitLength is 12 bit 
 * then client needs the 2 byte for the 1 packet. New packets start from the
 * new bytes e.g. byte0 and byte1 contain the first packet and byte2 and byte3
 * will contain the second packets.
 *
 * To perform one transaction, the BytesRequested argument should be:
 *
 *   (PacketSizeInBits + 7)/8
 *
 * To perform n transactions, BytesRequested should be:
 *
 *   n*((PacketSizeInBits + 7)/8)
 *
 * Within a given
 * transaction with the packet size larger than 8 bits, the bytes are stored in 
 * order of the MSB (most significant byte) first.
 * The Packet is formed with the first Byte will be in MSB and then next byte 
 * will be in the  next MSB towards the LSB.
 *	
 * For the example, if One packet need to be send and its size is the 20 bit 
 * then it will require the 3 bytes in the pWriteBuffer and arrangement of the 
 * data	 are as follows:
 * The packet is 0x000ABCDE (Packet with length of 20 bit).
 * pWriteBuff[0] = 0x0A
 * pWriteBuff[1] = 0xBC
 * pWtriteBuff[2] = 0xDE
 *
 * The most significant bit will be transmitted first i.e. bit20 is transmitted 
 * first and bit 0 will be transmitted last.
 *
 * If the transmitted packet (command + receive data) is more than 32 like 33 and 
 * want to transfer in the single call (CS should be active) then it can be transmitted
 * in following way:
 * The transfer is command(8 bit)+Dummy(1bit)+Read (24 bit) = 33 bit of transfer.
 * - Send 33 bit as 33 byte and each byte have the 1 valid bit, So packet bit length = 1 and
 * bytes requested = 33.
 * NvU8 pSendData[33], pRecData[33];
 *  pSendData[0] = (Comamnd >>7) & 0x1;
 *  pSendData[1] = (Command >> 6)& 0x1; 
 * ::::::::::::::
 * pSendData[8] = DummyBit;
 * pSendData[9] to pSendData[32] = 0;
 * Call NvRmSpiTransaction(hRmSpi,SpiPinMap,ChipSelect,ClockSpeedInKHz,pRecData, pSendData, 33,1);
 * Now You will get the read data from pRecData[9] to pRecData[32] on bit 0 on each byte.
 *
 * - The 33 bit transfer can be also done as 11 byte and each byte have the 3 valid bits.
 * This need to rearrange the command in the pSendData in such a way that each byte have the
 * 3 valid bits.
 * NvU8 pSendData[11], pRecData[11];
 *  pSendData[0] = (Comamnd >>4) & 0x7;
 *  pSendData[1] = (Command >> 1)& 0x7; 
 *  pSendData[2] = (((Command)& 0x3) <<1) | DummyBit; 
 * pSendData[3] to pSendData[10] = 0;
 * 
 * Call NvRmSpiTransaction(hRmSpi,SpiPinMap,ChipSelect,ClockSpeedInKHz,pRecData, pSendData, 11,3);
 * Now You will get the read data from pRecData[4] to pRecData[10] on lower 3 bits on each byte.
 *
 * Similarly the 33 bit transfer can also be done as 6 byte and each 2 bytes contain the 11 valid bits.
 * Call NvRmSpiTransaction(hRmSpi,SpiPinMap,ChipSelect,ClockSpeedInKHz,pRecData, pSendData, 6,11);
 *
 * pReadBuffer and pWriteBuffer may be the same pointer, in which case the 
 * write data is destroyed as we read in the read data. Unless they are 
 * identical pointers, however, pReadBuffer and pWriteBuffer must not overlap.
 *
 * @param hOdmSpi The Spi handle allocated in a call to NvOdmSpiOpen().
 * @param SpiPinMap For SPI master-mode controllers which are being multiplexed across
 *        multiple pin mux configurations, this specifies which pin mux configuration
 *        should be used for the transaction.  Must be 0 when the ODM pin mux query
 *        specifies a non-multiplexed configuration for the controller.
 * @param ChipSelectId The chip select Id on which device is connected.
 * @param ClockSpeedInKHz The clock speed in KHz on which device can communicate.
 * @param pReadBuffer A pointer to buffer to be filled in with read data. If this
 *     pointer is NULL, the read data will be discarded.
 * @param pWriteBuffer A pointer to a buffer from which to obtain write data. If this
 *     pointer is NULL, the write data will be all zeros.
 * @param BytesRequested The size of pReadBuffer and pWriteBuffer buffers in bytes.
 * @param PacketSizeInBits The packet size in bits of each Spi transaction.
 *
 */
 
 void NvRmSpiTransaction( 
    NvRmSpiHandle hRmSpi,
    NvU32 SpiPinMap,
    NvU32 ChipSelectId,
    NvU32 ClockSpeedInKHz,
    NvU8 * pReadBuffer,
    NvU8 * pWriteBuffer,
    NvU32 BytesRequested,
    NvU32 PacketSizeInBits );

/**
 * Start an Spi controller read and write simultaneously in the slave mode. 
 * This API is only supported for the spi handle which is opened in slave mode. 
 *
 * This API will assert if opened spi handle is the master type.
 *
 * Every Spi  transaction is by definition a simultaneous read and write 
 * transaction, so there are no separate APIs for read versus write. 
 * However, if you only need to start a read or write transaction, this API 
 * allows you to declare that you are not interested in the read data, 
 * or that the write data is not of interest.
 * If only read is required to start then client can pass NV_TRUE to the the
 * IsReadTransfer and NULL pointer to the pWriteBuffer. The state of the dataout 
 * will be set by IsIdleDataOutHigh of the structure NvOdmQuerySpiIdleSignalState 
 * in nvodm_query.h.
 * Similarly, if client wants to send data only then he can pass NV_FALSE to the
 * IsReadTransfer.
 *
 * This is a nonblocking API. This api start the data transfer and returns to the
 * caller without waiting for the data transfer completion.
 *
 * Transaction sizes from 1 to 32 bits are supported. However, all of the 
 * packets are byte-aligned in memory. Like, if packetBitLength is 12 bit 
 * then client needs the 2 byte for the 1 packet. New packets start from the
 * new bytes e.g. byte0 and byte1 contain the first packet and byte2 and byte3
 * will contain the second packets.
 *
 * To perform one transaction, the BytesRequested argument should be:
 *
 *   (PacketSizeInBits + 7)/8
 *
 * To perform n transactions, BytesRequested should be:
 *
 *   n*((PacketSizeInBits + 7)/8)
 *
 * Within a given
 * transaction with the packet size larger than 8 bits, the bytes are stored in 
 * order of the LSB (least significant byte) first.
 * The Packet is formed with the first Byte will be in LSB and then next byte 
 * will be in the  next LSB towards the MSB.
 *	
 * For the example, if One packet need to be send and its size is the 20 bit 
 * then it will require the 3 bytes in the pWriteBuffer and arrangement of the 
 * data	 are as follows:
 * The packet is 0x000ABCDE (Packet with length of 20 bit).
 * pWriteBuff[0] = 0xDE
 * pWriteBuff[1] = 0xBC
 * pWtriteBuff[2] = 0x0A
 *
 * The most significant bit will be transmitted first i.e. bit20 is transmitted 
 * first and bit 0 will be transmitted last.
 *
 * @see NvRmSpiGetTransactionData
 * Typical usecase for the CAIF interface.  The step for doing the transfer is:
 * 1. ACPU calls the NvRmSpiStartTransaction() to configure the spi controller
 * to set in the receive or transmit mode and make ready for the data transfer.
 * 2. ACPU then send the signal to the CCPU to send the SPICLK (by activating 
 * the SPI_INT) and start the transaction. CCPU get this signal and start sending 
 * SPICLK.
 * 3. ACPU will call the NvRmSpiGetTransactionData() to get the data/information 
 * about the transaction.
 * 4. After completion of the transfer ACPU inactivate the SPI_INT.
 *
 * @param hOdmSpi The Spi handle allocated in a call to NvOdmSpiOpen().
 * @param ChipSelectId The chip select Id on which device is connected.
 * @param ClockSpeedInKHz The clock speed in KHz on which device can communicate.
 * @param IsReadTransfer It tells that whether the read transfer is required or 
 * not. If it is NV_TRUE then read transfer is required and the read data will be 
 * available in the local buffer of the driver. The client will get the received
 * data after calling the NvRmSpiGetTransactionData().
 * @param pWriteBuffer A pointer to a buffer from which to obtain write data. If this
 *     pointer is NULL, the write data will be all zeros.
 * @param BytesRequested The size of pReadBuffer and pWriteBuffer buffers in bytes.
 * @param PacketSizeInBits The packet size in bits of each Spi transaction.
 *
 */
 
 NvError NvRmSpiStartTransaction( 
    NvRmSpiHandle hRmSpi,
    NvU32 ChipSelectId,
    NvU32 ClockSpeedInKHz,
    NvBool IsReadTransfer,
    NvU8 * pWriteBuffer,
    NvU32 BytesRequested,
    NvU32 PacketSizeInBits );

/**
 * Get the spi transaction status that is started for the slave mode and wait
 * if required till the transfer completes for a given timeout error.
 * If read transaction has been started then it will return the receive data to 
 * the client.
 *
 * This is a blocking API and wait for the data transfer completion till the
 * data requested transfer completes or the timeout happen.
 *
 * @see NvRmSpiStartTransaction
 *
 * @param hOdmSpi The Spi handle allocated in a call to NvOdmSpiOpen().
 * @param pReadBuffer A pointer to buffer to be filled in with read data. If this
 *     pointer is NULL, the read data will be discarded.
 * @param BytesRequested The size of pReadBuffer and pWriteBuffer buffers in bytes.
 * @param BytesTransfererd The number of bytes transferred.
 * @param WaitTimeout The timeout in millisecond to wait for the trsnaction to be 
 * completed.
 *
 * @retval NvSuccess Indicates that the operation succeeded.
 * @retval NvError_Timeout Indicates that the timeout happen.
 * @retval NvError_InvalidState Indicates that the transfer has not been started.
 *
 */
 
 NvError NvRmSpiGetTransactionData( 
    NvRmSpiHandle hRmSpi,
    NvU8 * pReadBuffer,
    NvU32 BytesRequested,
    NvU32 * pBytesTransfererd,
    NvU32 WaitTimeout );

/**
 * Set the signal mode for the spi communication for a given chip select.
 * After calling this API, the further communication happen with the new 
 * configured signal modes.
 * The default value of the signal mode is taken from nvodm query and this
 * api will override the signal mode which is read from query.
 *
 * @see NvRmSpiStartTransaction
 *
 * @param hOdmSpi The Spi handle allocated in a call to NvOdmSpiOpen().
 * @param ChipSelectId The chip select Id on which device is connected.
 * @param SpiSignalMode The nvodm signal modes which need to be set.
 *
 */
 
 void NvRmSpiSetSignalMode( 
    NvRmSpiHandle hRmSpi,
    NvU32 ChipSelectId,
    NvU32 SpiSignalMode );

#if defined(__cplusplus)
}
#endif

#endif
