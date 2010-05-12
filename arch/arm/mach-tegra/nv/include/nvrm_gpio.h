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

#ifndef INCLUDED_nvrm_gpio_H
#define INCLUDED_nvrm_gpio_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_init.h"


/** @file
 * @brief <b>NVIDIA Driver Development Kit NvRm gpio APIs</b>
 *
 * @b Description: Declares Interface for NvRm gpio module.
 */

 /**
 * @defgroup nvrm_gpio RM GPIO Services
 * 
 * This is the Resource Manager interface to general-purpose input-output
 * (GPIO) services. Fundamental abstraction of this API is a "pin handle", which
 * of type NvRmGpioPinHandle. A Pin handle is acquired by making a call to 
 * NvRmGpioAcquirePinHandle API. This API returns a pin handle which is
 * subsequently used by the rest of the GPIO APIs.
 *
 * @ingroup nvddk_rm
 * @{
 */

#include "nvcommon.h"
#include "nvos.h"

/**
 *  NvRmGpioHandle is an opaque handle to the GPIO device on the chip.
 */

typedef struct NvRmGpioRec *NvRmGpioHandle;

/**
 * @brief GPIO pin handle which describes the physical pin. This values should
 * not be cached or hardcoded by the drivers. This can vary from chip to chip
 * and board to board.
 */
 
typedef NvU32 NvRmGpioPinHandle;

/**
 * @brief Defines the possible gpio pin modes.
 */

typedef enum
{

    /**
     * Specifies the gpio pin as not in use. When in this state, the RM or
     * ODM Kit may park the pin in a board-specific state in order to
     * minimize leakage current.
     */
    NvRmGpioPinMode_Inactive = 1,

    /// Specifies the gpio pin mode as input and enable interrupt for level low.
    NvRmGpioPinMode_InputInterruptLow,

    /// Specifies the gpio pin mode as input and enable interrupt for level high.
    NvRmGpioPinMode_InputInterruptHigh,

    /// Specifies the gpio pin mode as input and no interrupt configured.
    NvRmGpioPinMode_InputData,

    /// Specifies the gpio pin mode as output.
    NvRmGpioPinMode_Output,

    /// Specifies the gpio pin mode as a special function.
    NvRmGpioPinMode_Function,

    /// Specifies the gpio pin as input and interrupt configured to any edge.
    /// i.e seamphore will be signaled for both the rising and failling edges.
    NvRmGpioPinMode_InputInterruptAny,

    /// Sepciifed the gpio pin a input and interrupt configured to rising edge.
    NvRmGpioPinMode_InputInterruptRisingEdge,

    /// Sepciifed the gpio pin a input and interrupt configured to falling edge.
    NvRmGpioPinMode_InputInterruptFallingEdge,
    NvRmGpioPinMode_Num,
    NvRmGpioPinMode_Force32 = 0x7FFFFFFF
} NvRmGpioPinMode;

/** 
 * @brief Defines the pin state
 */

typedef enum
{

   // Pin state high 
    NvRmGpioPinState_Low = 0,

    // Pin is high
    NvRmGpioPinState_High,
  
    // Pin is in tri state 
    NvRmGpioPinState_TriState,
    NvRmGpioPinState_Num,
    NvRmGpioPinState_Force32 = 0x7FFFFFFF
} NvRmGpioPinState;

// Gnerates a contruct the pin handle till the NvRmGpioAcquirePinHandle
// API is implemented.
#define GPIO_MAKE_PIN_HANDLE(inst, port, pin)  (0x80000000 | (((NvU32)(pin) & 0xFF)) | (((NvU32)(port) & 0xff) << 8) | (((NvU32)(inst) & 0xff )<< 16))
#define NVRM_GPIO_CAMERA_PORT (0xfe)
#define NVRM_GPIO_CAMERA_INST (0xfe)

/**
 * Creates and opens a GPIO handle. The handle can then be used to
 * access GPIO functions.
 * 
 * @param hRmDevice The RM device handle.
 * @param phGpio Specifies a pointer to the gpio handle where the
 * allocated handle is stored. The memory for handle is allocated
 * inside this API.
 * 
 * @retval NvSuccess gpio initialization is successful.
 */

 NvError NvRmGpioOpen( 
    NvRmDeviceHandle hRmDevice,
    NvRmGpioHandle * phGpio );

/**
 * Closes the GPIO handle. Any pin settings made while this handle was
 * open will remain. All events enabled by this handle will be
 * disabled.
 * 
 * @param hGpio A handle from NvRmGpioOpen().  If hGpio is NULL, this API does
 *     nothing.
 */

 void NvRmGpioClose( 
    NvRmGpioHandle hGpio );

/** Get NvRmGpioPinHandle from the physical port and pin number. If a driver
 * acquires a pin handle another driver will not be able to use this until the
 * pin is released.
 *
 * @param hGpio A handle from NvRmGpioOpen().
 * @param port Physical gpio ports which are chip specific.
 * @param pinNumber pin number in that port.
 * @param phGpioPin Pointer to the GPIO pin handle.
 */

 NvError NvRmGpioAcquirePinHandle( 
    NvRmGpioHandle hGpio,
    NvU32 port,
    NvU32 pin,
    NvRmGpioPinHandle * phPin );

/** Releases the pin handles acquired by NvRmGpioAcquirePinHandle API.
 *
 * @param hGpio A handle got from NvRmGpioOpen().
 * @param hPin Array of pin handles got from NvRmGpioAcquirePinHandle().
 * @param pinCount Size of pin handles array.
 */

 void NvRmGpioReleasePinHandles( 
    NvRmGpioHandle hGpio,
    NvRmGpioPinHandle * hPin,
    NvU32 pinCount );

/**
 * Sets the state of array of pins.
 *
 * NOTE:  When multiple pins are specified (pinCount is greater than
 * one), ODMs should not make assumptions about the order in which
 * pins are updated.  The implementation will attempt to coalesce
 * updates to occur atomically; however, this can not be guaranteed in
 * all cases, and may not occur if the list of pins includes pins from
 * multiple ports.
 *
 * @param hGpio Specifies the gpio handle.
 * @param pin Array of pin handles.
 * @param pinState Array of elements specifying the pin state (of type
 * NvRmGpioPinState).
 * @param pinCount Number of elements in the array.
 */

 void NvRmGpioWritePins( 
    NvRmGpioHandle hGpio,
    NvRmGpioPinHandle * pin,
    NvRmGpioPinState * pinState,
    NvU32 pinCount );

/**
 * Reads the state of array of pins.
 * 
 * @param hGpio The gpio handle.
 * @param pin Array of pin handles.
 * @param pinState Array of elements specifying the pin state (of type
 * NvRmGpioPinState).
 * @param pinCount Number of elements in the array.
 */

 void NvRmGpioReadPins( 
    NvRmGpioHandle hGpio,
    NvRmGpioPinHandle * pin,
    NvRmGpioPinState * pPinState,
    NvU32 pinCount );

/**
 * Configures a set of GPIO pins to a specified mode. Don't use this API for
 * the interrupt modes. For interrupt modes, use NvRmGpioInterruptRegister and
 * NvRmGpioInterruptUnregister APIs.
 *
 * @param hGpio The gpio handle.
 * @param pin Pin handle array returned by a calls to NvRmGpioAcquirePinHandle()
 * @param pinCount Number elements in the pin handle array.
 *
 * @param Mode Pin mode of type NvRmGpioPinMode.
 * 
 * 
 * @retval NvSuccess requested operation is successful.
 */

 NvError NvRmGpioConfigPins( 
    NvRmGpioHandle hGpio,
    NvRmGpioPinHandle * pin,
    NvU32 pinCount,
    NvRmGpioPinMode Mode );

/*
 *  Get the IRQs associated with the pin handles. So that the client can
 *  register the interrupt callback for that using interrupt APIs
 */

 NvError NvRmGpioGetIrqs( 
    NvRmDeviceHandle hRmDevice,
    NvRmGpioPinHandle * pin,
    NvU32 * Irq,
    NvU32 pinCount );

/** 
 *   Opaque handle to the GPIO interrupt.
 */

typedef struct NvRmGpioInterruptRec *NvRmGpioInterruptHandle;


/* NOTE: Use the 2 APIs below to configure the gpios to interrupt mode and to
 * have callabck functions. For the test case of how to use this APIs refer to
 * the nvrm_gpio_unit_test applicaiton. 
 * 
 *  Since the ISR is written by the clients of the API, care should be taken to
 *  clear the interrupt before the ISR is returned. If one fails to do that,
 *  interrupt will be triggered soon after the ISR returns.
 */

/**
 * Registers an interrupt callback function and the mode of interrupt for the
 * gpio pin specified.
 *  
 * Callback will be using the interrupt thread an the interrupt stack on linux
 * and IST on wince. So, care should be taken on what APIs can be used on the
 * callback function. Not all the nvos functions are available in the interrupt
 * context. Check the nvos.h header file for the list of the functions available.
 * When the callback is called, the interrupt on the pin is disabled. As soon as
 * the callback exists, the interrupt is re-enabled. So, external interrupts
 * should be cleared and then only the callback should be returned.
 *
 * @param hGpio The gpio handle.
 * @param hRm The RM device handle.
 * @param hPin The handle to a GPIO pin.
 * @param Callback Callback function which will be caused when the interrupt
 * triggers.
 * @param Mode Interrupt mode. See @NvRmGpioPinMode
 * @param CallbackArg Argument used when the callback is called by the ISR.
 * @param hGpioInterrupt Interrupt handle for this registered intterrupt. This
 * handle should be used while calling NvRmGpioInterruptUnregister for
 * unregistering the interrupt.
 * @param DebounceTime The debounce time in milliseconds
 * @retval NvSuccess requested operation is successful.
 */
NvError 
NvRmGpioInterruptRegister(
    NvRmGpioHandle hGpio,
    NvRmDeviceHandle hRm,
    NvRmGpioPinHandle hPin, 
    NvOsInterruptHandler Callback,
    NvRmGpioPinMode Mode,
    void *CallbackArg,
    NvRmGpioInterruptHandle *hGpioInterrupt,
    NvU32 DebounceTime);

/**
 * Unregister the GPIO interrupt handler.
 *
 * @param hGpio The gpio handle.
 * @param hRm The RM device handle.
 * @param handle The interrupt handle returned by a successfull call to the
 * NvRmGpioInterruptRegister API.
 *
 */
void 
NvRmGpioInterruptUnregister(
    NvRmGpioHandle hGpio,
    NvRmDeviceHandle hRm,
    NvRmGpioInterruptHandle handle);

/**
 * Enable the GPIO interrupt handler.
 *
 * @param handle The interrupt handle returned by a successfull call to the
 * NvRmGpioInterruptRegister API.
 *
 * @retval "NvError_BadParameter" if handle is not valid
 * @retval "NvError_InsufficientMemory" if interupt enable failed.
 * @retval "NvSuccess" if registration is successfull.
*/
NvError
NvRmGpioInterruptEnable(NvRmGpioInterruptHandle handle);

/* 
 * Callback used to re-enable the interrupts.
 *
 * @param handle The interrupt handle returned by a successfull call to the
 * NvRmGpioInterruptRegister API.
 */
void
NvRmGpioInterruptDone( NvRmGpioInterruptHandle handle );



/**
 * Mask/Unmask a gpio interrupt.
 *
 * Drivers can use this API to fend off interrupts. Mask means interrupts are
 * not forwarded to the CPU. Unmask means, interrupts are forwarded to the CPU.
 * In case of SMP systems, this API masks the interrutps to all the CPU, not
 * just the calling CPU.
 *
 *
 * @param handle    Interrupt handle returned by NvRmGpioInterruptRegister API.
 * @param mask      NV_FALSE to forrward the interrupt to CPU. NV_TRUE to 
 * mask the interupts to CPU.
 */
void
NvRmGpioInterruptMask(NvRmGpioInterruptHandle hGpioInterrupt, NvBool mask);


/** @} */

#if defined(__cplusplus)
}
#endif

#endif
