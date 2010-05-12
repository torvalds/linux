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

#ifndef INCLUDED_nvrm_i2c_H
#define INCLUDED_nvrm_i2c_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_pinmux.h"
#include "nvrm_module.h"
#include "nvrm_init.h"

#include "nvos.h"
#include "nvcommon.h"

/**
 * NvRmI2cHandle is an opaque handle to the NvRmI2cStructRec interface
 */
 
typedef struct NvRmI2cRec *NvRmI2cHandle;

/**
 * @brief Defines the I2C capability structure. It contains the 
 * capabilities/limitations (like maximum bytes transferred,  
 * supported clock speed) of the hardware.
 */
 
typedef struct NvRmI2cCapabilitiesRec
{

    /**
     * Maximum number of packet length in bytes which can be transferred
     * between start and the stop pulses.
     */
    NvU32 MaximumPacketLengthInBytes;

    /// Maximum speed which I2C controller can support.
    NvU32 MaximumClockSpeed;

    /// Minimum speed which I2C controller can support.
    NvU32 MinimumClockSpeed;
} NvRmI2cCapabilities;

/**
 * @brief Initializes and opens the i2c channel. This function allocates the
 * handle for the i2c channel and provides it to the client.
 *
 * Assert encountered in debug mode if passed parameter is invalid.
 *
 * @param hDevice Handle to the Rm device which is required by Rm to acquire
 * the resources from RM.
 * @param IoModule The IO module to set, it is either NvOdmIoModule_I2c
 * or NvOdmIoModule_I2c_Pmu
 * @param instance Instance of the i2c driver to be opened.
 * @param phI2c Points to the location where the I2C handle shall be stored.
 *
 * @retval NvSuccess Indicates that the I2c channel has successfully opened.
 * @retval NvError_InsufficientMemory Indicates that function fails to allocate
 * the memory.
 * @retval NvError_NotInitialized Indicates the I2c initialization failed.
 */

 NvError NvRmI2cOpen( 
    NvRmDeviceHandle hDevice,
    NvU32 IoModule,
    NvU32 instance,
    NvRmI2cHandle * phI2c );

/** 
 * @brief Closes the i2c channel. This function frees the memory allocated for
 * the i2c handle for the i2c channel.
 * This function de-initializes the i2c channel. This API never fails.
 *
 * @param hI2c A handle from NvRmI2cOpen().  If hI2c is NULL, this API does
 *     nothing.
 */

 void NvRmI2cClose( 
    NvRmI2cHandle hI2c );

// Maximum number of bytes that can be sent between the i2c start and stop conditions
#define NVRM_I2C_PACKETSIZE (8)

// Maximum number of bytes that can be sent between the i2c start and repeat start condition.
#define NVRM_I2C_PACKETSIZE_WITH_NOSTOP (4)

/// Indicates a I2C read transaction.
#define NVRM_I2C_READ (0x1)

/// Indicates that it is a write transaction
#define NVRM_I2C_WRITE (0x2)

/// Indicates that there is no STOP following this transaction. This also implies
/// that there is always one more transaction following a transaction with
/// NVRM_I2C_NOSTOP attribute.
#define NVRM_I2C_NOSTOP (0x4)

//  Some devices doesn't support ACK. By, setting this flag, master will not
//  expect the generation of ACK from the device.
#define NVRM_I2C_NOACK (0x8)

// Software I2C using GPIO. Doesn't use the hardware controllers. This path
// should be used only for testing.
#define NVRM_I2C_SOFTWARE_CONTROLLER (0x10)

typedef struct NvRmI2cTransactionInfoRec
{

    /// Flags to indicate the transaction details, like write/read or read
    /// without a stop or write without a stop.
        NvU32 Flags;

    /// Number of bytes to be transferred.
        NvU32 NumBytes;

    /// I2C slave device address
        NvU32 Address;

    /// Indicates that the address is a 10-bit address.
        NvBool Is10BitAddress;
} NvRmI2cTransactionInfo;

/** 
 * @brief Does multiple I2C transactions. Each transaction can be a read or write.
 *
 *  AP15 I2C controller has the following limitations:
 *  - Any read/write transaction is limited to NVRM_I2C_PACKETSIZE
 *  - All transactions will be terminated by STOP unless NVRM_I2C_NOSTOP flag
 *  is specified. Specifying NVRM_I2C_NOSTOP means, *next* transaction will start
 *  with a repeat start, with NO stop between transactions.
 *  - When NVRM_I2C_NOSTOP is specified for a transaction - 
 *      1. Next transaction will start with repeat start.
 *      2. Next transaction is mandatory.
 *      3. Next Next transaction cannot have NVRM_I2C_NOSTOP flag set. i.e no
 *         back to back repeat starts.
 *      4. Current and next transactions are limited to size
 *         NVRM_I2C_PACKETSIZE_WITH_NOSTOP.
 *      5. Finally, current transactions and next Transaction should be of same
 *         size.
 *
 *  This imposes some limitations on how the hardware can be used. However, the
 *  API itself doesn't have any limitations. If the HW cannot be used, it falls
 *  back to GPIO based I2C. Gpio I2C bypasses Hw controller and bit bangs the
 *  SDA/SCL lines of I2C.
 * 
 * @param hI2c Handle to the I2C channel.
 * @param I2cPinMap for I2C controllers which are being multiplexed across
 *        multiple pin mux configurations, this specifies which pin mux configuration
 *        should be used for the transaction.  Must be 0 when the ODM pin mux query
 *        specifies a non-multiplexed configuration for the controller.
 * @param WaitTimeoutInMilliSeconds Timeout for the transcation. 
 * @param ClockSpeedKHz Clock speed in KHz.
 * @param Data Continous stream of data
 * @param DataLength Length of the data stream
 * @param Transcations Pointer to the NvRmI2cTransactionInfo structure
 * @param NumOfTransactions Number of transcations
 *
 *
 * @retval NvSuccess Indicates the operation succeeded.
 * @retval NvError_NotSupported Indicates assumption on parameter values violated.
 * @retval NvError_InvalidState Indicates that the last read call is not
 * completed.
 * @retval NvError_ControllerBusy Indicates controller is presently busy with an
 * i2c transaction.
 * @retval NvError_InvalidDeviceAddress Indicates that the slave device address
 * is invalid
 */

 NvError NvRmI2cTransaction( 
    NvRmI2cHandle hI2c,
    NvU32 I2cPinMap,
    NvU32 WaitTimeoutInMilliSeconds,
    NvU32 ClockSpeedKHz,
    NvU8 * Data,
    NvU32 DataLen,
    NvRmI2cTransactionInfo * Transaction,
    NvU32 NumOfTransactions );

#if defined(__cplusplus)
}
#endif

#endif
