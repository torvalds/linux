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

/**
 * @file
 * <b>NVIDIA Tegra ODM Kit:
 *         External GPIO Interface</b>
 *
 * @b Description: Defines the ODM adaptation interface for GPIO
 *    pins that are sourced from off-chip peripherals.
 */

#ifndef INCLUDED_NVODM_GPIO_EXT_H
#define INCLUDED_NVODM_GPIO_EXT_H

#include "nvcommon.h"
#include "nvodm_services.h"

/**
 * @defgroup nvodm_gpio_ext External GPIO Interface
 *
 * Your clients do not use the API functions defined here. Instead, they
 * make use of the ::NvOdmExternalGpioPort enumeration, which defines 
 * logical GPIO ports that may be included in the Peripheral DB entries.
 * The ODM developer is responsible to write the external GPIO handler in
 * your ODM Adaptation implementation.
 * 
 * This feature makes it possible to treat GPIOs sourced from an external
 * peripheral as if they came from the Tegra application processor itself
 * (thus, allowing for the use of the NvOdmGpio* functions). This makes
 * it possible for some adaptation client implementations to remain
 * unchanged if the pin gets moved from an external to an internal device.
 *
 * For instance, the PMU sources a GPIO pin for the backlight, but the
 * display adaptation just requests the GPIO for this function from the
 * Peripheral DB. The PMU adaptation implements the actual handling of
 * the external GPIO. As an alternative, the backlight could be switched
 * on or off via a GPIO pin from the chip. In that case, the peripheral
 * DB will get updated, but the display adaptation can remain unchanged,
 * which is the advantage of this feature.
 *
 * @ingroup nvodm_adaptation
 * @{
 */

#if defined(__cplusplus)
extern "C"
{
#endif


/** External GPIO device context. */
typedef struct GpioExtDeviceRec* NvOdmGpioExtHandle;

/**
 * @brief Defines the external GPIO ports.  These definitions 
 *        are generic placeholders, as they map to off-chip
 *        GPIOs as defined by the ODM. The ODM is
 *        responsible for documenting and using these
 *        pre-defined ports consistently between their
 *        adaptation client and their implementation of the
 *        Peripheral Discovery DB entries.
 */
typedef enum {
    NVODM_GPIO_EXT_PORT_0 = 0xD0,
    NVODM_GPIO_EXT_PORT_1,
    NVODM_GPIO_EXT_PORT_2,
    NVODM_GPIO_EXT_PORT_3,
    NVODM_GPIO_EXT_PORT_4,
    NVODM_GPIO_EXT_PORT_5,
    NVODM_GPIO_EXT_PORT_6,
    NVODM_GPIO_EXT_PORT_7,
    NVODM_GPIO_EXT_PORT_8,
    NVODM_GPIO_EXT_PORT_9,
    NVODM_GPIO_EXT_PORT_A,
    NVODM_GPIO_EXT_PORT_B,
    NVODM_GPIO_EXT_PORT_C,
    NVODM_GPIO_EXT_PORT_D,
    NVODM_GPIO_EXT_PORT_E,
    NVODM_GPIO_EXT_PORT_F,
} NvOdmExternalGpioPort;

/**
 * This is an ODM-specific adaptation that writes the output 
 * state of external (off-chip) GPIO pins for the specified 
 * port. This function is not called directly by the client 
 * that uses the external GPIOs, but rather called indirectly 
 * via NvOdmGpioSetState().
 *
 * @sa NvOdmGpioOpen(), NvOdmExternalGpioReadPins()
 *
 * @param Port The specified external GPIO port.
 * @param Pin The specified GPIO pin.
 * @param PinValue The pin state to set. 0 means drive low, 1 means drive high.
 */
void
NvOdmExternalGpioWritePins(
    NvU32 Port,
    NvU32 Pin,
    NvU32 PinValue);

/**
 * This is an ODM-specific adaptation that reads the output 
 * state of external (off-chip) GPIO pins for the specified 
 * port. This function is not called directly by the client 
 * that uses the external GPIOs, but rather called indirectly 
 * via NvOdmGpioGetState(). 
 *
 * @sa NvOdmGpioOpen(), NvOdmExternalGpioWritePins()
 *  
 * @param Port The specified external GPIO port.
 * @param Pin The specified GPIO pin. 
 *  
 * @return The current state of the specified port+pin.
 */
NvU32
NvOdmExternalGpioReadPins(
    NvU32 Port,
    NvU32 Pin);

#if defined(__cplusplus)
}
#endif

/** @} */
#endif  // INCLUDED_NVODM_GPIO_EXT_H
