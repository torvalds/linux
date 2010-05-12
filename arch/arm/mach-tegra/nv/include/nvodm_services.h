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
 * <b>NVIDIA Tegra ODM Kit:
 *         ODM Services API</b>
 *
 * @b Description: Defines the abstraction to SOC resources used by
 *                 external peripherals.
 */

#ifndef INCLUDED_NVODM_SERVICES_H
#define INCLUDED_NVODM_SERVICES_H

// Using addtogroup when defgroup resides in another file
/**
 * @addtogroup nvodm_services
 * @{
 */

#include "nvcommon.h"
#include "nvodm_modules.h"
#include "nvassert.h"
#include "nvcolor.h"
#include "nvodm_query_pinmux.h"
#include "nvodm_query.h"

#if defined(__cplusplus)
extern "C"
{
#endif

/*
 * This header is split into two sections: OS abstraction APIs and basic I/O
 * driver APIs.
 */

/** @name OS Abstraction APIs
 * The Operating System APIs are portable to any NVIDIA-supported operating
 * system and will appear in all of the engineering sample code.
 */
/*@{*/

/**
 * Outputs a message to the console, if present. Do not use this for
 * interacting with a user from an application.
 *
 * @param format A pointer to the format string. The format string and variable
 * parameters exactly follow the posix printf standard.
 */
void
NvOdmOsPrintf( const char *format, ...);

/**
 * Outputs a message to the debugging console, if present. Do not use this for
 * interacting with a user from an application.
 *
 * @param format A pointer to the format string. The format string and variable
 * parameters exactly follow the posix printf standard.
 */
void
NvOdmOsDebugPrintf( const char *format, ... );

/**
 * Dynamically allocates memory. Alignment, if desired, must be done by the
 * caller.
 *
 * @param size The size, in bytes, of the allocation request.
 */
void *
NvOdmOsAlloc(size_t size);

/**
 * Frees a dynamic memory allocation.
 *
 * Freeing a NULL value is supported.
 *
 * @param ptr A pointer to the memory to free, which should be from NvOdmOsAlloc().
 */
void
NvOdmOsFree(void *ptr);

typedef struct NvOdmOsMutexRec *NvOdmOsMutexHandle;
typedef struct NvOdmOsSemaphoreRec *NvOdmOsSemaphoreHandle;
typedef struct NvOdmOsThreadRec *NvOdmOsThreadHandle;

/**
 * Copies a specified number of bytes from a source memory location to
 * a destination memory location.
 *
 *  @param dest A pointer to the destination of the copy.
 *  @param src A pointer to the source memory.
 *  @param size The length of the copy in bytes.
 */
void
NvOdmOsMemcpy(void *dest, const void *src, size_t size);

/**
 * Sets a region of memory to a value.
 *
 *  @param s A pointer to the memory region.
 *  @param c The value to set.
 *  @param size The length of the region in bytes.
 */
void
NvOdmOsMemset(void *s, NvU8 c, size_t size);

/**
 * Create a new mutex.
 *
 * @note Mutexes can be locked recursively; if a thread owns the lock,
 * it can lock it again as long as it unlocks it an equal number of times.
 *
 * @return NULL on failure.
 */
NvOdmOsMutexHandle
NvOdmOsMutexCreate( void );

/**
 * Locks the given unlocked mutex.
 *
 * @note This is a recursive lock.
 *
 * @param mutex The mutex to lock.
 */
void
NvOdmOsMutexLock( NvOdmOsMutexHandle mutex );

/**
 * Unlock a locked mutex.
 *
 * A mutex must be unlocked exactly as many times as it has been locked.
 *
 * @param mutex The mutex to unlock.
 */
void
NvOdmOsMutexUnlock( NvOdmOsMutexHandle mutex );

/**
 * Frees the resources held by a mutex.
 *
 * @param mutex The mutex to destroy. Passing a NULL mutex is supported.
 */
void
NvOdmOsMutexDestroy( NvOdmOsMutexHandle mutex );

/**
 * Creates a counting semaphore.
 *
 * @param value The initial semaphore value.
 *
 * @return NULL on failure.
 */
NvOdmOsSemaphoreHandle
NvOdmOsSemaphoreCreate( NvU32 value );

/**
 * Waits until the semaphore value becomes non-zero.
 *
 * @param semaphore The semaphore for which to wait.
 */
void
NvOdmOsSemaphoreWait( NvOdmOsSemaphoreHandle semaphore );

/**
 * Waits for the given semaphore value to become non-zero with timeout.
 *
 * @param semaphore The semaphore for which to wait.
 * @param msec The timeout value in milliseconds. Use ::NV_WAIT_INFINITE
 * to wait forever.
 *
 * @return NV_FALSE if the wait expires.
 */
NvBool
NvOdmOsSemaphoreWaitTimeout( NvOdmOsSemaphoreHandle semaphore, NvU32 msec );

/**
 * Increments the semaphore value.
 *
 * @param semaphore The semaphore to signal.
 */
void
NvOdmOsSemaphoreSignal( NvOdmOsSemaphoreHandle semaphore );

/**
 * Frees resources held by the semaphore.
 *
 * @param semaphore The semaphore to destroy. Passing in a NULL semaphore
 * is supported (no op).
 */
void
NvOdmOsSemaphoreDestroy( NvOdmOsSemaphoreHandle semaphore );

/**
 * Entry point for a thread.
 */
typedef void (*NvOdmOsThreadFunction)(void *args);

/**
 * Creates a thread.
 *
 *  @param function The thread entry point.
 *  @param args The thread arguments.
 *
 * @return The thread handle, or NULL on failure.
 */
NvOdmOsThreadHandle
NvOdmOsThreadCreate(
    NvOdmOsThreadFunction function,
    void *args);

/**
 * Waits for the given thread to exit.
 *
 *  The joined thread will be destroyed automatically. All OS resources
 *  will be reclaimed. There is no method for terminating a thread
 *  before it exits naturally.
 *
 *  Passing in a NULL thread ID is ok (no op).
 *
 *  @param thread The thread to wait for.
 */
void
NvOdmOsThreadJoin(NvOdmOsThreadHandle thread);

/**
 *  Unschedules the calling thread for at least the given
 *      number of milliseconds.
 *
 *  Other threads may run during the sleep time.
 *
 *  @param msec The number of milliseconds to sleep. This API should not be
 *  called from an ISR, can be called from the IST though!
 */
void
NvOdmOsSleepMS(NvU32 msec);


/**
 * Stalls the calling thread for at least the given number of
 * microseconds. The actual time waited might be longer, so you cannot
 * depend on this function for precise timing.
 *
 * @note It is safe to use this function at ISR time.
 *
 * @param usec The number of microseconds to wait.
 */
void
NvOdmOsWaitUS(NvU32 usec);

/**
 * Gets the system time in milliseconds.
 * The returned values are guaranteed to be monotonically increasing,
 * but may wrap back to zero (after about 50 days of runtime).
 *
 * @return The system time in milliseconds.
 */
NvU32
NvOdmOsGetTimeMS(void);

/// Defines possible operating system types.
typedef enum
{
    NvOdmOsOs_Unknown,
    NvOdmOsOs_Windows,
    NvOdmOsOs_Linux,
    NvOdmOsOs_Aos,
    NvOdmOsOs_Force32 = 0x7fffffffUL,
} NvOdmOsOs;

/// Defines possible operating system SKUs.
typedef enum
{
    NvOdmOsSku_Unknown,
    NvOdmOsSku_CeBase,
    NvOdmOsSku_Mobile_SmartFon,
    NvOdmOsSku_Mobile_PocketPC,
    NvOdmOsSku_Android,
    NvOdmOsSku_Force32 = 0x7fffffffUL,
} NvOdmOsSku;

/// Defines the OS information record.
typedef struct NvOdmOsOsInfoRec
{
    NvOdmOsOs  OsType;
    NvOdmOsSku Sku;
    NvU16   MajorVersion;
    NvU16   MinorVersion;
    NvU32   SubVersion;
    NvU32   Caps;
} NvOdmOsOsInfo;

/**
 * Gets the current OS version.
 *
 * @param pOsInfo A pointer to the OS version.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmOsGetOsInformation( NvOdmOsOsInfo *pOsInfo );

/*@}*/
/** @name Basic I/O Driver APIs
 * The basic I/O driver APIs are a set of common input/outputs
 * that can be used to extend the functionality of the software stack
 * for new devices that aren't explicity handled by the stack.
 * GPIO, I2C, and SPI are currently supported.
*/
/*@{*/

/**
 * Defines an opaque handle to the ODM Services GPIO rec interface.
 */
typedef struct NvOdmServicesGpioRec *NvOdmServicesGpioHandle;
/**
 * Defines an opaque handle to the ODM Services GPIO intr interface.
 */
typedef struct NvOdmServicesGpioIntrRec *NvOdmServicesGpioIntrHandle;
/**
 * Defines an opaque handle to the ODM Services SPI interface.
 */
typedef struct NvOdmServicesSpiRec *NvOdmServicesSpiHandle;
/**
 * Defines an opaque handle to the ODM Services I2C interface.
 */
typedef struct NvOdmServicesI2cRec *NvOdmServicesI2cHandle;
/**
 * Defines an opaque handle to the ODM Services PMU interface.
 */
typedef struct NvOdmServicesPmuRec *NvOdmServicesPmuHandle;
/**
 * Defines an opaque handle to the ODM Services PWM interface.
 */
typedef struct NvOdmServicesPwmRec *NvOdmServicesPwmHandle;
/**
 * Defines an opaque handle to the ODM Services key list interface.
 */
typedef struct NvOdmServicesKeyList *NvOdmServicesKeyListHandle;

/**
 * Defines an interrupt handler.
 */
typedef void (*NvOdmInterruptHandler)(void *args);

/**
 * @brief Defines the possible GPIO pin modes.
 */
typedef enum
{
    /// Specifies that that the pin is tristated, which will consume less power.
    NvOdmGpioPinMode_Tristate = 1,

    /// Specifies input mode with active low interrupt.
    NvOdmGpioPinMode_InputInterruptLow,

    /// Specifies input mode with active high interrupt.
    NvOdmGpioPinMode_InputInterruptHigh,

    /// Specifies input mode with no events.
    NvOdmGpioPinMode_InputData,

    /// Specifies output mode.
    NvOdmGpioPinMode_Output,

    /// Specifies special function.
    NvOdmGpioPinMode_Function,

    /// Specifies input and interrupt on any edge.
    NvOdmGpioPinMode_InputInterruptAny,

    /// Specifies input and interrupt on rising edge.
    NvOdmGpioPinMode_InputInterruptRisingEdge,

    /// Specifies output and interrupt on falling edge.
    NvOdmGpioPinMode_InputInterruptFallingEdge,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmGpioPinMode_Force32 = 0x7fffffff

} NvOdmGpioPinMode;

/**
 * Defines the opaque handle to the GPIO pin.
 */
typedef struct NvOdmGpioPinRec *NvOdmGpioPinHandle;

/**
 * Creates and opens a GPIO handle. The handle can then be used to
 * access GPIO functions.
 *
 * @see NvOdmGpioClose
 *
 * @return The handle to the GPIO controller, or NULL if an error occurred.
 */
NvOdmServicesGpioHandle NvOdmGpioOpen(void);

/**
 * Closes the GPIO handle. Any pin settings made while this handle is
 * open will remain. All events enabled by this handle are
 * disabled.
 *
 * @see NvOdmGpioOpen
 *
 * @param hOdmGpio The GPIO handle.
 */
void NvOdmGpioClose(NvOdmServicesGpioHandle hOdmGpio);

/**
 * Acquires a pin handle to be used in subsequent calls to
 * access the pin.
 *
 * @see NvOdmGpioClose
 *
 * @param hOdmGpio The GPIO handle.
 * @param port The port.
 * @param Pin The pin for which to return the handle.
 *
 * @return The pin handle, or NULL if an error occurred.
 */
NvOdmGpioPinHandle
NvOdmGpioAcquirePinHandle(NvOdmServicesGpioHandle hOdmGpio,
        NvU32 port, NvU32 Pin);

/**
 * Releases the pin handle that was acquired by NvOdmGpioAcquirePinHandle()
 * and used by the rest of the GPIO ODM APIs.
 *
 * @see NvOdmGpioAcquirePinHandle
 *
 * @param hOdmGpio The GPIO handle.
 * @param hPin The pin handle to release.
 */
void
NvOdmGpioReleasePinHandle(NvOdmServicesGpioHandle hOdmGpio,
        NvOdmGpioPinHandle hPin);
/**
 * Sets the output state of a set of GPIO pins.
 *
 * @see NvOdmGpioOpen, NvOdmGpioGetState
 *
 * @param hOdmGpio The GPIO handle.
 * @param hGpioPin The pin handle.
 * @param PinValue The pin state to set. 0 means drive low, 1 means drive high.
 */
void
NvOdmGpioSetState(NvOdmServicesGpioHandle hOdmGpio,
    NvOdmGpioPinHandle hGpioPin,
    NvU32 PinValue);

/**
 * Gets the output state of a specified set of GPIO pins in the port.
 *
 * @see NvOdmGpioOpen, NvOdmGpioSetState
 *
 * @param hOdmGpio The GPIO handle.
 * @param hGpioPin The pin handle.
 * @param pPinStateValue A pointer to the returned current state of the pin.
 */
void
NvOdmGpioGetState(NvOdmServicesGpioHandle hOdmGpio,
    NvOdmGpioPinHandle hGpioPin,
    NvU32 *pPinStateValue);

/**
 * Configures the GPIO to specific mode. Don't use this API to configure the pin
 * as interrupt pin, instead use the NvOdmGpioInterruptRegister
 *  and NvOdmGpioInterruptUnregister APIs which internally call this function.
 *
 * @param hOdmGpio  The GPIO handle.
 * @param hGpioPin  The pin handle.
 * @param Mode      The mode type to configure.
 */
void
NvOdmGpioConfig(NvOdmServicesGpioHandle hOdmGpio,
    NvOdmGpioPinHandle hGpioPin,
    NvOdmGpioPinMode Mode);

/**
 * Registers an interrupt callback function and the mode of interrupt for the
 * GPIO pin specified.
 *
 * Callback uses the interrupt thread and the interrupt stack on Linux
 * and IST on Windows CE; so, care should be taken on all the APIs used in
 * the callback function.
 *
 * Interrupts are masked when they are triggered. It is up to the caller to
 * re-enable the interrupts by calling NvOdmGpioInterruptDone().
 *  
 * @param hOdmGpio  The GPIO handle.
 * @param hGpioIntr A pointer to the GPIO interrupt handle. Use this 
 *  handle while unregistering the interrupt. On failure to hook
 *  up the interrupt, a NULL handle is returned.
 * @param hGpioPin  The pin handle.
 * @param Mode      The mode type to configure. Allowed mode values are:
 *  - NvOdmGpioPinMode_InputInterruptFallingEdge
 *  - NvOdmGpioPinMode_InputInterruptRisingEdge
 *  - NvOdmGpioPinMode_InputInterruptAny
 *  - NvOdmGpioPinMode_InputInterruptLow
 *  - NvOdmGpioPinMode_InputInterruptHigh
 *  
 * @param Callback The callback function that is called when 
 *  the interrupt triggers.
 * @param arg The argument used when the callback is called by the ISR. 
 * @param DebounceTime The debounce time in milliseconds.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmGpioInterruptRegister(NvOdmServicesGpioHandle hOdmGpio,
    NvOdmServicesGpioIntrHandle *hGpioIntr,
    NvOdmGpioPinHandle hGpioPin,
    NvOdmGpioPinMode Mode,
    NvOdmInterruptHandler Callback,
    void *arg,
    NvU32 DebounceTime);

/**
 *  Client of GPIO interrupt to re-enable the interrupt after
 *  the handling the interrupt.
 *
 *  @param handle GPIO interrupt handle returned by a sucessfull call to
 *  NvOdmGpioInterruptRegister().
 */
void NvOdmGpioInterruptDone( NvOdmServicesGpioIntrHandle handle );

/**
 * Mask/Unmask a gpio interrupt.
 *
 * Drivers can use this API to fend off interrupts. Mask means interrupts are
 * not forwarded to the CPU. Unmask means, interrupts are forwarded to the CPU.
 * In case of SMP systems, this API masks the interrutps to all the CPU, not
 * just the calling CPU.
 *
 *
 * @param handle    Interrupt handle returned by NvOdmGpioInterruptRegister API.
 * @param mask      NV_FALSE to forrward the interrupt to CPU. NV_TRUE to 
 * mask the interupts to CPU.
 */
void
NvOdmGpioInterruptMask(NvOdmServicesGpioIntrHandle handle, NvBool mask);

/**
 * Unregisters the GPIO interrupt handler.
 *
 * @param hOdmGpio  The GPIO handle.
 * @param hGpioPin  The pin handle.
 * @param handle The interrupt handle returned by a successfull call to
 * NvOdmGpioInterruptRegister().
 *
 */
void
NvOdmGpioInterruptUnregister(NvOdmServicesGpioHandle hOdmGpio,
    NvOdmGpioPinHandle hGpioPin,
    NvOdmServicesGpioIntrHandle handle);

/**
 * Obtains a handle that can be used to access one of the serial peripheral
 * interface (SPI) controllers.
 *
 * There may be one or more instances of the SPI, depending upon the SOC,
 * and these instances start from 0.
 *
 * @see NvOdmSpiClose
 *
 * @param OdmIoModule  The ODM I/O module for the SFLASH, SPI, or SLINK.
 * @param ControllerId  The SPI controlled ID for which a handle is required.
 *     Valid SPI channel IDs start from 0.
 *
 *
 * @return The handle to the SPI controller, or NULL if an error occurred.
 */
NvOdmServicesSpiHandle NvOdmSpiOpen(NvOdmIoModule OdmIoModule, NvU32 ControllerId);

/**
 * Obtains a handle that can be used to access one of the serial peripheral
 * interface (SPI) controllers, for SPI controllers which are multiplexed
 * between multiple pin mux configurations. The SPI controller's pin mux
 * will be reset to the specified value every transaction, so that two handles
 * to the same controller may safely interleave across pin mux configurations.
 *
 * The ODM pin mux query for the specified controller must be
 * NvOdmSpiPinMap_Multiplexed in order to create a handle using this function.
 *
 * There may be one or more instances of the SPI, depending upon the SOC,
 * and these instances start from 0.
 *
 * Currently, this function is only supported for OdmIoModule_Spi, instance 2.
 *
 * @see NvOdmSpiClose
 *
 * @param OdmIoModule  The ODM I/O module for the SFLASH, SPI, or SLINK.
 * @param ControllerId  The SPI controlled ID for which a handle is required.
 *     Valid SPI channel IDs start from 0.
 * @param PinMap The pin mux configuration to use for every transaction.
 *
 * @return The handle to the SPI controller, or NULL if an error occurred.
 */
NvOdmServicesSpiHandle 
NvOdmSpiPinMuxOpen(NvOdmIoModule OdmIoModule,
                   NvU32 ControllerId,
                   NvOdmSpiPinMap PinMap);


/**
 * Obtains a handle that can be used to access one of the serial peripheral
 * interface (SPI) controllers in slave mode.
 *
 * There may be one or more instances of the SPI, depending upon the SOC,
 * and these instances start from 0.
 *
 * @see NvOdmSpiClose
 *
 * @param OdmIoModule  The ODM I/O module for the SFLASH, SPI, or SLINK.
 * @param ControllerId  The SPI controlled ID for which a handle is required.
 *     Valid SPI channel IDs start from 0.
 *
 *
 * @return The handle to the SPI controller, or NULL if an error occurred.
 */
NvOdmServicesSpiHandle NvOdmSpiSlaveOpen(NvOdmIoModule OdmIoModule, NvU32 ControllerId);


/**
 * Releases a handle to an SPI controller. This API must be called once per
 * successful call to NvOdmSpiOpen().
 *
 * @param hOdmSpi A SPI handle allocated in a call to \c NvOdmSpiOpen.  If \em hOdmSpi
 *     is NULL, this API has no effect.
 */
void NvOdmSpiClose(NvOdmServicesSpiHandle hOdmSpi);

/**
 * Performs an SPI controller transaction. Every SPI transaction is by
 * definition a simultaneous read and write transaction, so there are no
 * separate APIs for read versus write. However, if you only need to do a read or
 * write, this API allows you to declare that you are not interested in the read
 * data, or that the write data is not of interest.
 *
 * This is a blocking API. When it returns, all of the data has been sent out
 * over the pins of the SOC (the transaction). This is true even if the read data
 * is being discarded, as it cannot merely have been queued up.
 *
 * Several SPI transactions may be performed in a single call to this API, but
 * only if all of the transactions are to the same chip select and have the same
 * packet size.
 *
 * Transaction sizes from 1 bit to 32 bits are supported. However, all
 * of the buffers in memory are byte-aligned. To perform one transaction,
 * the \em Size argument should be:
 *
 * <tt> <!-- typewriter font formats this nicely in the output document -->
 *   (PacketSize + 7)/8
 * </tt>
 *
 * To perform n transactions, \em Size should be:
 *
 * <tt>
 *   n*((PacketSize + 7)/8)
 * </tt>
 *
 * Within a given
 * transaction with the packet size larger than 8 bits, the bytes are stored in 
 * order of the MSB (most significant byte) first.
 * The Packet is formed with the first Byte will be in MSB and then next byte 
 * will be in the  next MSB towards the LSB.
 *
 * For the example, if One packet need to be send and its size is the 20 bit 
 * then it will require the 3 bytes in the pWriteBuffer and arrangement of the 
 * data  are as follows:
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
 * Call NvOdmSpiTransaction(hRmSpi,ChipSelect,ClockSpeedInKHz,pRecData, pSendData, 33,1);
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
 * Call NvOdmSpiTransaction(hRmSpi, ChipSelect,ClockSpeedInKHz,pRecData, pSendData, 11,3);
 * Now You will get the read data from pRecData[4] to pRecData[10] on lower 3 bits on each byte.
 *
 * Similarly the 33 bit transfer can also be done as 6 byte and each 2 bytes contain the 11 valid bits.
 * Call NvOdmSpiTransaction(hRmSpi, ChipSelect,ClockSpeedInKHz,pRecData, pSendData, 6,11);
 *
 *
 * \em ReadBuf and \em WriteBuf may be the same pointer, in which case the write
 * data is destroyed as we read in the read data. Unless they are identical pointers,
 * however, \em ReadBuf and \em WriteBuf must not overlap.
 *
 * @param hOdmSpi The SPI handle allocated in a call to NvOdmSpiOpen().
 * @param ChipSelect Select with which of the several external devices (attached
 *     to a single controller) we are communicating. Chip select indices
 *     start at 0.
 * @param ClockSpeedInKHz The speed in kHz on which the device can communicate.
 * @param ReadBuf A pointer to buffer to be filled in with read data. If this
 *     pointer is NULL, the read data will be discarded.
 * @param WriteBuf A pointer to a buffer from which to obtain write data. If this
 *     pointer is NULL, the write data will be all zeros.
 * @param Size The size of \em ReadBuf and \em WriteBuf buffers in bytes.
 * @param PacketSize The packet size in bits of each SPI transaction.
 */
void
NvOdmSpiTransaction(
    NvOdmServicesSpiHandle hOdmSpi,
    NvU32 ChipSelect,
    NvU32 ClockSpeedInKHz,
    NvU8 *ReadBuf,
    const NvU8 *WriteBuf,
    NvU32 Size,
    NvU32 PacketSize);


/**
 * Starts an SPI controller read and write simultaneously in the slave mode.
 *
 * This is a nonblocking API, which starts the data transfer and returns
 * to the caller without waiting for the data transfer completion. 
 *
 * @note This API is only supported for the SPI handle, which is opened in
 * slave mode using NvOdmSpiSlaveOpen(). This API asserts if the opened SPI
 * handle is the master type.
 *
 * @see NvOdmSpiSlaveGetTransactionData
 *
 * @par Read or Write Transactions
 *
 * Every SPI transaction is by definition a simultaneous read and write 
 * transaction, so there are no separate APIs for read versus write. 
 * However, if you only need to start a read or write transaction, this API 
 * allows you to declare that you are not interested in the read data, 
 * or that the write data is not of interest. If only read
 * is required to start, then the client can pass NV_TRUE to the \a IsReadTransfer
 * parameter and a NULL pointer to \a pWriteBuffer. The state of the data out 
 * will be set by NvOdmQuerySpiIdleSignalState::IsIdleDataOutHigh  
 * in nvodm_query.h. Similarly, if the client wants to send data only
 * then it can pass NV_FALSE to the \a IsReadTransfer parameter.
 *
 * @par Transaction Sizes
 *
 * Transaction sizes from 1 to 32 bits are supported. However, all of the 
 * packets are byte-aligned in memory. So, if \a packetBitLength is 12 bits 
 * then the client needs the 2nd byte for the 1 packet. New packets start from the
 * new bytes, e.g., byte0 and byte1 contain the first packet and byte2 and byte3
 * will contain the second packets.
 *
 * To perform one transaction, the \a BytesRequested argument should be:
 * <pre>
 *   (PacketSizeInBits + 7)/8
 * </pre>
 *
 * To perform \a n transactions, \a BytesRequested should be:
 * <pre>
 *   n*((PacketSizeInBits + 7)/8)
 * </pre>
 *
 * Within a given transaction with the packet size larger than 8 bits,
 * the bytes are stored in the order of the LSB (least significant byte) first.
 * The packet is formed with the first byte will be in LSB and then next byte 
 * will be in the next LSB towards the MSB.
 *  
 * For example, if one packet needs to be sent and its size is 20 bits, 
 * then it will require the 3 bytes in the \a pWriteBuffer and arrangement of
 * the data  are as follows:
 * - The packet is 0x000ABCDE (Packet with length of 20 bit).
 * - pWriteBuff[0] = 0xDE
 * - pWriteBuff[1] = 0xBC
 * - pWtriteBuff[2] = 0x0A
 *
 * The most significant bit will be transmitted first, i.e., bit20 is transmitted 
 * first and bit0 will be transmitted last.
 *
 * @par Transfer Size Limitations
 *
 * The limitation on the maximum transfer size of SPI slave communication
 * depends upon the hardware. The maximum size of byte transfer is 64 K bytes
 * if the number of packets requested is a multiple of: 
 * - 4 for 8-bit packet length, or 
 * - 2 for 16-bit packet length, or 
 * - any number of packets for 32-bit packet length. 
 *
 * For all other cases, the maximum transfer bytes size is limited to 16 K
 * packets, that is:
 * <pre>
 * 16K*((PacketBitLength +7)/8))
 * </pre>
 * 
 * For the example: 
 * - Non-multiples of 4 for the 8-bit packet length 
 * - Non multiples of 2 for the 16-bit packet length 
 * - Any other bit length except for the 32-bit packet length
 * 
 * This limitation comes from the:
 * - Maximum HW DMA transfer of 64 KB
 * - Maximum packet transfer for HW S-LINK controller of 64 K packets 
 * - The design of packed/unpacked format of the S-LINK controller
 *
 * @par CAIF Use Case
 * 
 * The following describes a typical use case for the CAIF interface. The steps
 * for doing the transfer are:
 * -# ACPU calls the NvOdmSpiSlaveStartTransaction() to configure the SPI 
 * controller to set in the receive or transmit mode and make ready for the 
 * data transfer.
 * -# ACPU then send the signal to the CCPU to send the SPICLK (by activating 
 * the SPI_INT) and start the transaction. CCPU get this signal and start sending 
 * SPICLK.
 * -# ACPU will call the NvOdmSpiSlaveGetTransactionData() to get the 
 * data/information about the transaction.
 * -# After completion of the transfer ACPU inactivate the SPI_INT.
 *
 * @param hOdmSpi The SPI handle allocated in a call to NvOdmSpiSlaveOpen().
 * @param ChipSelectId The chip select ID on which device is connected.
 * @param ClockSpeedInKHz The clock speed in kHz on which device can communicate.
 * @param IsReadTransfer Tells that whether or not the read transfer is required.
 * If it is NV_TRUE then read transfer is required and the read data will be 
 * available in the local buffer of the driver. The client will get the received
 * data after calling the \c NvRmSpiGetTransactionData() function.
 * @param pWriteBuffer A pointer to a buffer from which to obtain write data. 
 * If this pointer is NULL, the write data will be all zeros.
 * @param BytesRequested The size of \a pReadBuffer and \a pWriteBuffer buffers
 * in bytes.
 * @param PacketSizeInBits The packet size in bits of each SPI transaction.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
 NvBool NvOdmSpiSlaveStartTransaction( 
    NvOdmServicesSpiHandle hOdmSpi,
    NvU32 ChipSelectId,
    NvU32 ClockSpeedInKHz,
    NvBool IsReadTransfer,
    NvU8 * pWriteBuffer,
    NvU32 BytesRequested,
    NvU32 PacketSizeInBits );

/**
 * Gets the SPI transaction status that is started for the slave mode and waits,
 * if required, until the transfer completes for a given timeout error.
 * If a read transaction has been started, then it returns the receive data to 
 * the client.
 *
 * This is a blocking API and waits for the data transfer completion until the
 * transfer completes or a timeout happens.
 *
 * @see NvOdmSpiSlaveStartTransaction
 *
 * @param hOdmSpi The SPI handle allocated in a call to NvOdmSpiSlaveOpen().
 * @param pReadBuffer A pointer to a buffer to be filled in with read data. If this
 * pointer is NULL, the read data will be discarded.
 * @param BytesRequested The size of \a pReadBuffer and \a pWriteBuffer buffers
 * in bytes.
 * @param pBytesTransfererd A pointer to the number of bytes transferred.
 * @param WaitTimeout The timeout in millisecond to wait for the transaction to be 
 * completed.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 *
 */
  NvBool NvOdmSpiSlaveGetTransactionData( 
    NvOdmServicesSpiHandle hOdmSpi,
    NvU8 * pReadBuffer,
    NvU32 BytesRequested,
    NvU32 * pBytesTransfererd,
    NvU32 WaitTimeout );

/**
 * Sets the signal mode for the SPI communication for a given chip select.
 * After calling this API, further communication happens with the newly 
 * configured signal modes.
 * The default value of the signal mode is taken from ODM Query, and this
 * API overrides the signal mode that is read from the query.
 *
 * @param hOdmSpi The SPI handle allocated in a call to NvOdmSpiOpen().
 * @param ChipSelectId The chip select ID to which the device is connected.
 * @param SpiSignalMode The ODM signal mode to be set.
 *
 */
void
NvOdmSpiSetSignalMode(
    NvOdmServicesSpiHandle hOdmSpi,
    NvU32 ChipSelectId,
    NvOdmQuerySpiSignalMode SpiSignalMode);

/// Contains the error flags for the I2C transaction.
typedef enum
{
    NvOdmI2cStatus_Success = 0,
    NvOdmI2cStatus_Timeout,
    NvOdmI2cStatus_SlaveNotFound,
    NvOdmI2cStatus_InvalidTransferSize,
    NvOdmI2cStatus_ReadFailed,
    NvOdmI2cStatus_WriteFailed,
    NvOdmI2cStatus_InternalError,
    NvOdmI2cStatus_ArbitrationFailed,
    NvOdmI2cStatus_Force32 = 0x7FFFFFFF
} NvOdmI2cStatus;

/// Flag to indicate the I2C write/read operation.
#define NVODM_I2C_IS_WRITE            0x00000001
/// Flag to indicate the I2C slave address type as 10-bit or 7-bit.
#define NVODM_I2C_IS_10_BIT_ADDRESS   0x00000002
/// Flag to indicate the I2C transaction with repeat start.
#define NVODM_I2C_USE_REPEATED_START  0x00000004
/// Flag to indicate that the I2C slave will not generate ACK.
#define NVODM_I2C_NO_ACK              0x00000008
/// Flag to indicate software I2C using GPIO.
#define NVODM_I2C_SOFTWARE_CONTROLLER 0x00000010


/// Contians the I2C transaction details.
typedef struct
{
    /// Flags to indicate the transaction details, like write/read operation,
    /// slave address type 10-bit or 7-bit and the transaction uses repeat
    /// start or a normal transaction.
    NvU32 Flags;
    /// I2C slave device address.
    NvU32 Address;
    /// Number of bytes to be transferred.
    NvU32 NumBytes;
    /// Send/receive buffer. For I2C send operation this buffer should be
    /// filled with the data to be sent to the slave device. For I2C receive
    /// operation this buffer is filled with the data received from the slave device.
    NvU8 *Buf;
} NvOdmI2cTransactionInfo;

/**
 * Initializes and opens the I2C channel. This function allocates the
 * handle for the I2C channel and provides it to the client.
 *
 * @see NvOdmI2cClose
 *
 * @param OdmIoModuleId  The ODM I/O module for I2C.
 * @param instance The instance of the I2C driver to be opened.
 *
 * @return The handle to the I2C controller, or NULL if an error occurred.
 */
NvOdmServicesI2cHandle
NvOdmI2cOpen(
    NvOdmIoModule OdmIoModuleId,
    NvU32 instance);

/**
 * Obtains a handle that can be used to access one of the I2C controllers, 
 * for I2C controllers which are multiplexed between multiple pin mux 
 * configurations. The I2C controller's pin mux will be reset to the specified 
 * value every transaction, so that two handles to the same controller may 
 * safely interleave across pin mux configurations.
 *
 * The ODM pin mux query for the specified controller must be
 * NvOdmI2cPinMap_Multiplexed in order to create a handle using this function.
 *
 * There may be one or more instances of the I2C, depending upon the SOC,
 * and these instances start from 0.
 *
 * Currently, this function is only supported for OdmIoModule_I2C, instance 1.
 *
 * @see NvOdmI2cClose
 *
 * @param OdmIoModule  The ODM I/O module for the I2C.
 * @param ControllerId  The I2C controlled ID for which a handle is required.
 *     Valid I2C controller IDs start from 0.
 * @param PinMap The pin mux configuration to use for every transaction.
 *
 * @return The handle to the I2C controller, or NULL if an error occurred.
 */
NvOdmServicesI2cHandle 
NvOdmI2cPinMuxOpen(NvOdmIoModule OdmIoModule,
                   NvU32 ControllerId,
                   NvOdmI2cPinMap PinMap);

/**
 * Closes the I2C channel. This function frees the memory allocated for
 * the I2C handle and de-initializes the I2C ODM channel.
 *
 * @see NvOdmI2cOpen
 *
 * @param hOdmI2c The handle to the I2C channel.
 */
void NvOdmI2cClose(NvOdmServicesI2cHandle hOdmI2c);

/**
 * Does the I2C send or receive transactions with the slave deivces. This is a
 * blocking call (with timeout). This API works for both the normal I2C transactions
 * or I2C transactions in repeat start mode.
 *
 * For the I2C transactions with slave devices, a pointer to the list of required
 * transactions must be passed and the corresponding number of transactions must
 * be passed.
 *
 * The transaction information structure contains the flags (to indicate the
 * transaction information, such as read or write transaction, transaction is with
 * repeat-start or normal transaction and the slave device address type is 7-bit or
 * 10-bit), slave deivce address, buffer to be transferred and number of bytes
 * to be transferred.
 *
 * @param hOdmI2c The handle to the I2C channel.
 * @param TransactionInfo A pointer to the array of I2C transaction structures.
 * @param NumberOfTransactions The number of I2C transactions.
 * @param ClockSpeedKHz Specifies the clock speed for the I2C transactions.
 * @param WaitTimeoutInMilliSeconds The timeout in milliseconds.
 *  ::NV_WAIT_INFINITE specifies to wait forever.
 *
 * @retval NvOdmI2cStatus_Success If successful, or the appropriate error code.
 */
NvOdmI2cStatus
NvOdmI2cTransaction(
    NvOdmServicesI2cHandle hOdmI2c,
    NvOdmI2cTransactionInfo *TransactionInfo,
    NvU32 NumberOfTransactions,
    NvU32 ClockSpeedKHz,
    NvU32 WaitTimeoutInMilliSeconds);

/**
 *  Defines the PMU VDD rail capabilities.
 */
typedef struct NvOdmServicesPmuVddRailCapabilitiesRec
{
    /// Specifies ODM protection attribute; if \c NV_TRUE PMU hardware
    ///  or ODM Kit would protect this voltage from being changed by NvDdk client.
    NvBool RmProtected;

    /// Specifies the minimum voltage level in mV.
    NvU32 MinMilliVolts;

    /// Specifies the step voltage level in mV.
    NvU32 StepMilliVolts;

    /// Specifies the maximum voltage level in mV.
    NvU32 MaxMilliVolts;

    /// Specifies the request voltage level in mV.
    NvU32 requestMilliVolts;

} NvOdmServicesPmuVddRailCapabilities;

/// Special level to indicate voltage plane is disabled.
#define NVODM_VOLTAGE_OFF (0UL)

/**
 * Initializes and opens the PMU driver. The handle that is returned by this
 * driver is used for all the other PMU operations.
 *
 * @see NvOdmPmuClose
 *
 * @return The handle to the PMU driver, or NULL if an error occurred.
 */
NvOdmServicesPmuHandle NvOdmServicesPmuOpen(void);

/**
 * Closes the PMU handle.
 *
 * @see NvOdmServicesPmuOpen
 *
 * @param handle The handle to the PMU driver.
 */
void NvOdmServicesPmuClose(NvOdmServicesPmuHandle handle);

/**
 * Gets capabilities for the specified PMU rail.
 *
 * @param handle The handle to the PMU driver.
 * @param vddId The ODM-defined PMU rail ID.
 * @param pCapabilities A pointer to the targeted
 *  capabilities returned by the ODM.
 *
 */
void NvOdmServicesPmuGetCapabilities(
        NvOdmServicesPmuHandle handle,
        NvU32 vddId,
        NvOdmServicesPmuVddRailCapabilities * pCapabilities );

/**
 * Gets current voltage level for the specified PMU rail.
 *
 * @param handle The handle to the PMU driver.
 * @param vddId The ODM-defined PMU rail ID.
 * @param pMilliVolts A pointer to the voltage level returned
 *  by the ODM.
 */
void NvOdmServicesPmuGetVoltage(
        NvOdmServicesPmuHandle handle,
        NvU32 vddId,
        NvU32 * pMilliVolts );

/**
 * Sets new voltage level for the specified PMU rail.
 *
 * @param handle The handle to the PMU driver.
 * @param vddId The ODM-defined PMU rail ID.
 * @param MilliVolts The new voltage level to be set in millivolts (mV).
 *  Set to ::NVODM_VOLTAGE_OFF to turn off the target voltage.
 * @param pSettleMicroSeconds A pointer to the settling time in microseconds (uS),
 *  which is the time for supply voltage to settle after this function
 *  returns; this may or may not include PMU control interface transaction time,
 *  depending on the ODM implementation. If NULL this parameter is ignored.
 */
void NvOdmServicesPmuSetVoltage(
        NvOdmServicesPmuHandle handle,
        NvU32 vddId,
        NvU32 MilliVolts,
        NvU32 * pSettleMicroSeconds );

/**
 * Configures SoC power rail controls for the upcoming PMU voltage transition.
 *
 * @note Should be called just before PMU rail On/Off, or Off/On transition.
 *  Should not be called if rail voltage level is changing within On range.
 * 
 * @param handle The handle to the PMU driver.
 * @param vddId The ODM-defined PMU rail ID.
 * @param Enable Set NV_TRUE if target voltage is about to be turned On, or
 *  NV_FALSE if target voltage is about to be turned Off.
 */
void NvOdmServicesPmuSetSocRailPowerState(
        NvOdmServicesPmuHandle handle,
        NvU32 vddId, 
        NvBool Enable );

/**
 * Defines battery instances.
 */
typedef enum
{
    /// Specifies main battery.
    NvOdmServicesPmuBatteryInst_Main,

    /// Specifies backup battery.
    NvOdmServicesPmuBatteryInst_Backup,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmServicesPmuBatteryInstance_Force32 = 0x7FFFFFFF
} NvOdmServicesPmuBatteryInstance;

/**
 * Gets the battery status.
 *
 * @param handle The handle to the PMU driver.
 * @param batteryInst The battery type.
 * @param pStatus A pointer to the battery
 *  status returned by the ODM.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool 
NvOdmServicesPmuGetBatteryStatus(
    NvOdmServicesPmuHandle handle,
    NvOdmServicesPmuBatteryInstance batteryInst,
    NvU8 * pStatus);

/**
 * Defines battery data.
 */
typedef struct NvOdmServicesPmuBatteryDataRec
{
    /// Specifies battery life percent.
    NvU32 batteryLifePercent;

    /// Specifies battery life time.
    NvU32 batteryLifeTime;

    /// Specifies voltage.
    NvU32 batteryVoltage;
    
    /// Specifies battery current.
    NvS32 batteryCurrent;

    /// Specifies battery average current.
    NvS32 batteryAverageCurrent;

    /// Specifies battery interval.
    NvU32 batteryAverageInterval;

    /// Specifies the mAH consumed.
    NvU32 batteryMahConsumed;

    /// Specifies battery temperature.
    NvU32 batteryTemperature;
} NvOdmServicesPmuBatteryData;

/**
 * Gets the battery data.
 *
 * @param handle The handle to the PMU driver.
 * @param batteryInst The battery type.
 * @param pData A pointer to the battery
 *  data returned by the ODM.
 * 
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmServicesPmuGetBatteryData(
    NvOdmServicesPmuHandle handle,
    NvOdmServicesPmuBatteryInstance batteryInst,
    NvOdmServicesPmuBatteryData * pData);

/**
 * Gets the battery full lifetime.
 *
 * @param handle The handle to the PMU driver.
 * @param batteryInst The battery type.
 * @param pLifeTime A pointer to the battery
 *  full lifetime returned by the ODM.
 */
void
NvOdmServicesPmuGetBatteryFullLifeTime(
    NvOdmServicesPmuHandle handle,
    NvOdmServicesPmuBatteryInstance batteryInst,
    NvU32 * pLifeTime);

/**
 * Defines battery chemistry.
 */
typedef enum
{
    /// Specifies an alkaline battery.
    NvOdmServicesPmuBatteryChemistry_Alkaline,

    /// Specifies a nickel-cadmium (NiCd) battery.
    NvOdmServicesPmuBatteryChemistry_NICD,

    /// Specifies a nickel-metal hydride (NiMH) battery.
    NvOdmServicesPmuBatteryChemistry_NIMH,

    /// Specifies a lithium-ion (Li-ion) battery.
    NvOdmServicesPmuBatteryChemistry_LION,

    /// Specifies a lithium-ion polymer (Li-poly) battery.
    NvOdmServicesPmuBatteryChemistry_LIPOLY,

    /// Specifies a zinc-air battery.
    NvOdmServicesPmuBatteryChemistry_XINCAIR,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmServicesPmuBatteryChemistry_Force32 = 0x7FFFFFFF
} NvOdmServicesPmuBatteryChemistry;

/**
 * Gets the battery chemistry.
 *
 * @param handle The handle to the PMU driver.
 * @param batteryInst The battery type.
 * @param pChemistry A pointer to the battery
 *  chemistry returned by the ODM.
 */
void
NvOdmServicesPmuGetBatteryChemistry(
    NvOdmServicesPmuHandle handle,
    NvOdmServicesPmuBatteryInstance batteryInst,
    NvOdmServicesPmuBatteryChemistry * pChemistry);

/**
 * Defines the charging path.
 */
typedef enum
{
    /// Specifies external wall plug charger.
    NvOdmServicesPmuChargingPath_MainPlug, 

    /// Specifies external USB bus charger.
    NvOdmServicesPmuChargingPath_UsbBus,

    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmServicesPmuChargingPath_Force32 = 0x7FFFFFFF
} NvOdmServicesPmuChargingPath;

/** 
* Sets the charging current limit. 
* 
* @param handle The Rm device handle.
* @param ChargingPath The charging path. 
* @param ChargingCurrentLimitMa The charging current limit in mA. 
* @param ChargerType The charger type.
*/
void 
NvOdmServicesPmuSetChargingCurrentLimit( 
    NvOdmServicesPmuHandle handle,
    NvOdmServicesPmuChargingPath ChargingPath,
    NvU32 ChargingCurrentLimitMa,
    NvOdmUsbChargerType ChargerType);

/**
 * Obtains a handle to set or get state of keys, for example, the state of the
 * hold switch.
 *
 * @see NvOdmServicesKeyListClose()
 *
 * @return A handle to the key-list, or NULL if this open call fails.
 */
NvOdmServicesKeyListHandle
NvOdmServicesKeyListOpen(void);

/**
 * Releases the handle obtained during the NvOdmServicesKeyListOpen() call and
 * any other resources allocated.
 *
 * @param handle The handle returned from the \c NvOdmServicesKeyListOpen call.
 */
void NvOdmServicesKeyListClose(NvOdmServicesKeyListHandle handle);

/**
 * Searches the list of keys present and returns the value of the appropriate
 * key.
 * @param handle The handle obtained from NvOdmServicesKeyListOpen().
 * @param KeyID The ID of the key whose value is required.
 *
 * @return The value of the corresponding key, or 0 if the key is not
 * present in the list.
 */
NvU32
NvOdmServicesGetKeyValue(
            NvOdmServicesKeyListHandle handle,
            NvU32 KeyID);

/**
 * Searches the list of keys present and sets the value of the key to the value
 * given. If the key is not present, it adds the key to the list and sets the
 * value.
 * @param handle The handle obtained from NvOdmServicesKeyListOpen().
 * @param Key The ID of the key whose value is to be set.
 * @param Value The value to be set for the corresponding key.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmServicesSetKeyValuePair(
            NvOdmServicesKeyListHandle handle,
            NvU32 Key,
            NvU32 Value);

/**
 * @brief Defines the possible PWM modes.
 */

typedef enum
{
    /// Specifies Pwm disabled mode.
    NvOdmPwmMode_Disable = 1,

    /// Specifies Pwm enabled mode.
    NvOdmPwmMode_Enable,

    /// Specifies Blink LED enabled mode
    NvOdmPwmMode_Blink_LED,

    /// Specifies Blink output 32KHz clock enable mode
    NvOdmPwmMode_Blink_32KHzClockOutput,

    /// Specifies Blink disabled mode
    NvOdmPwmMode_Blink_Disable,

    NvOdmPwmMode_Force32 = 0x7fffffffUL

} NvOdmPwmMode;

/**
 * @brief Defines the possible PWM output pin.
 */

typedef enum
{
    /// Specifies PWM Output-0.
    NvOdmPwmOutputId_PWM0 = 1,

    /// Specifies PWM Output-1.
    NvOdmPwmOutputId_PWM1,

    /// Specifies PWM Output-2.
    NvOdmPwmOutputId_PWM2,

    /// Specifies PWM Output-3.
    NvOdmPwmOutputId_PWM3,

    /// Specifies PMC Blink LED.
    NvOdmPwmOutputId_Blink, 

    NvOdmPwmOutputId_Force32 = 0x7fffffffUL

} NvOdmPwmOutputId;

/**
 * Creates and opens a PWM handle. The handle can be used to
 * access PWM functions.
 *
 * @note Only the service client knows when the service can go idle,
 * like in the case of vibrator, so the client suspend entry code
 * must call NvOdmPwmClose() to close the PWM service.
 *
 * @return The handle to the PWM controller, or NULL if an error occurred.
 */
NvOdmServicesPwmHandle NvOdmPwmOpen(void);

/**
 * Releases a handle to a PWM controller. This API must be called once per
 * successful call to NvOdmPwmOpen().
 *
 * @param hOdmPwm The handle to the PWM controller.
 */
void NvOdmPwmClose(NvOdmServicesPwmHandle hOdmPwm);

/**
 *  @brief Configures PWM module as disable/enable. This API is also
 *  used to set the PWM duty cycle and frequency. Beside that, it is 
 *  used to configure PMC' blinking LED if OutputId is 
 *  NvOdmPwmOutputId_Blink
 *
 *  @param hOdmPwm The PWM handle obtained from NvOdmPwmOpen().
 *  @param OutputId The PWM output pin to configure. Allowed values are
 *   defined in ::NvOdmPwmOutputId.
 *  @param Mode The mode type to configure. Allowed values are
 *   defined in ::NvOdmPwmMode.
 *  @param DutyCycle The duty cycle is an unsigned 15.16 fixed point
 *   value that represents the PWM duty cycle in percentage range from
 *   0.00 to 100.00. For example, 10.5 percentage duty cycle would be
 *   represented as 0x000A8000. This parameter is ignored if NvOdmPwmMode
 *   is NvOdmMode_Blink_32KHzClockOutput or NvOdmMode_Blink_Disable
 *  @param pRequestedFreqHzOrPeriod A pointer to the request frequency in Hz
 *   or period in second
 *   A requested frequency value beyond the maximum supported value will be
 *   clamped to the maximum supported value. If \em pRequestedFreqHzOrPeriod 
 *   is NULL, it returns the maximum supported frequency. This parameter is 
 *   ignored if NvOdmPwmMode is NvOdmMode_Blink_32KHzClockOutput or
 *   NvOdmMode_Blink_Disable
 *  @param pCurrentFreqHzOrPeriod A pointer to the returned frequency of 
 *   that mode. If PMC Blink LED is used then it is the pointer to the returns 
 *   period time. This parameter is ignored if NvOdmPwmMode is
 *   NvOdmMode_Blink_32KHzClockOutput or NvOdmMode_Blink_Disable
 */
void
NvOdmPwmConfig(NvOdmServicesPwmHandle hOdmPwm,
    NvOdmPwmOutputId OutputId, 
    NvOdmPwmMode Mode,            
    NvU32 DutyCycle,
    NvU32 *pRequestedFreqHzOrPeriod,
    NvU32 *pCurrentFreqHzOrPeriod);

/**
 * Enables and disables external clock interfaces (e.g., CDEV and CSUS pins)
 * for the specified peripheral. External clock sources should be enabled
 * prior to programming peripherals reliant on them. If multiple peripherals use
 * the same external clock source, it is safe to call this API multiple times.
 *
 * @param Guid The ODM-defined GUID of the peripheral to be configured. The
 *             peripheral should have an @see NvOdmIoAddress entry for the
 *             NvOdmIoModule_ExternalClock device interface. If multiple
 *             external clock interfaces are specified, all will be
 *             enabled (disabled).
 *
 * @param EnableTristate NV_TRUE will tristate the specified clock sources,
 *             NV_FALSE will drive them.
 *  
 * @param pInstances Returns the list of clocks that were enabled.
 *  
 * @param pFrequencies Returns the frequency, in kHz, that is 
 *                     being output on each clock pin
 *  
 * @param pNum Returns the number of clocks that were enabled.
 *
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmExternalClockConfig(
    NvU64 Guid,
    NvBool EnableTristate,
    NvU32 *pInstances,
    NvU32 *pFrequencies,
    NvU32 *pNum);

/**
 *  Defines SoC strap groups.
 */
typedef enum
{
    /// Specifies the ram_code strap group.
    NvOdmStrapGroup_RamCode = 1,

    NvOdmStrapGroup_Num,
    NvOdmStrapGroup_Force32 = 0x7FFFFFFF
} NvOdmStrapGroup;

/**
 * Gets SoC strap value for the given strap group.
 * 
 * @note The strap assignment on each platform must be consistent with SoC
 *  bootrom specifications and platform-specific BCT contents. The strap
 *  value usage in ODM queries, however, is not limited to bootrom defined
 *  functionality. The mapping between strap values and platforms is the ODM's
 *  responsibility. ODMs should also ensure that they are using strap groups
 *  that match the SOC in their product.
 * 
 * @param StrapGroup The strap group to be read.
 * @param pStrapValue A pointer to the returned strap group value.
 *  This value can be used by ODM queries to identify ODM platforms and to
 *  provide the respective configuration settings.
 *  
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool NvOdmGetStraps(NvOdmStrapGroup StrapGroup, NvU32* pStrapValue);

/**
 * File input/output.
 */
typedef void* NvOdmOsFileHandle;

/**
 *  Defines the OS file types.
 */
typedef enum
{
    NvOdmOsFileType_Unknown = 0,
    NvOdmOsFileType_File,
    NvOdmOsFileType_Directory,
    NvOdmOsFileType_Fifo,

    NvOdmOsFileType_Force32 = 0x7FFFFFFF
} NvOdmOsFileType;

/**
 *  Defines the OS status type.
 */
typedef struct NvOdmOsStatTypeRec
{
    NvU64 size;
    NvOdmOsFileType type;
} NvOdmOsStatType;

/** Open a file with read permissions. */
#define NVODMOS_OPEN_READ    0x1

/** Open a file with write persmissions. */
#define NVODMOS_OPEN_WRITE   0x2

/** Create a file if is not present on the file system. */
#define NVODMOS_OPEN_CREATE  0x4

/**
 *  Opens a file stream.
 *
 *  If the ::NVODMOS_OPEN_CREATE flag is specified, ::NVODMOS_OPEN_WRITE must also
 *  be specified.
 *
 *  If ::NVODMOS_OPEN_WRITE is specified the file will be opened for write and
 *  will be truncated if it was previously existing.
 *
 *  If ::NVODMOS_OPEN_WRITE and ::NVODMOS_OPEN_READ is specified the file will not
 *  be truncated.
 *
 *  @param path A pointer to the path to the file.
 *  @param flags ORed flags for the open operation (NVODMOS_OPEN_*).
 *  @param file [out] A pointer to the file that will be opened, if successful.
 *
 *  @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmOsFopen(const char *path, NvU32 flags, NvOdmOsFileHandle *file);

/**
 *  Closes a file stream.
 *  Passing in a NULL handle is okay.
 *
 *  @param stream The file stream to close.
 */
void NvOdmOsFclose(NvOdmOsFileHandle stream);

/**
 *  Writes to a file stream.
 *
 *  @param stream The file stream.
 *  @param ptr A pointer to the data to write.
 *  @param size The length of the write.
 *
 *  @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmOsFwrite(NvOdmOsFileHandle stream, const void *ptr, size_t size);

/**
 *  Reads a file stream.
 *
 *  To detect short reads (less that specified amount), pass in \a bytes
 *  and check its value to the expected value. The \a bytes parameter may
 *  be NULL.
 *
 *  @param stream The file stream.
 *  @param ptr A pointer to the buffer for the read data.
 *  @param size The length of the read.
 *  @param bytes [out] A pointer to the number of bytes read -- may be NULL.
 *
 *  @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmOsFread(NvOdmOsFileHandle stream, void *ptr, size_t size, size_t *bytes);

/**
 *  Gets file information.
 *
 *  @param filename A pointer to the file to get information about.
 *  @param stat [out] A pointer to the information structure.
 *
 *  @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmOsStat(const char *filename, NvOdmOsStatType *stat);

/**
 * Enables or disables USB OTG circuitry.
 *
 * @param Enable NV_TRUE to enable, or NV_FALSE to disable.
 */
void NvOdmEnableOtgCircuitry(NvBool Enable);

/**
 * Checks whether or not USB is connected.
 *
 * @pre The USB circuit is enabled by calling NvOdmEnableOtgCircuitry().
 * To reduce power consumption, disable the USB circuit when not connected
 * by calling \c NvOdmEnableOtgCircuitry(NV_FALSE).
 *
 * @return NV_TRUE if USB is successfully connected, otherwise NV_FALSE.
 */
NvBool NvOdmUsbIsConnected(void);

/**
 * Checks the current charging type.
 *
 * @pre The USB circuit is enabled by calling NvOdmEnableOtgCircuitry().
 * To reduce power consumption, disable the USB circuit when not connected
 * by calling \c NvOdmEnableOtgCircuitry(NV_FALSE).
 *
 * @param Instance Set to 0 by default.
 * @return The current charging type.
 */
NvOdmUsbChargerType NvOdmUsbChargingType(NvU32 Instance);

#if defined(__cplusplus)
}
#endif

/*@}*/
/** @} */

#endif // INCLUDED_NVODM_SERVICES_H
