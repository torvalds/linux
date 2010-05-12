/*
 * Copyright (c) 2008-2009 NVIDIA Corporation.
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
 *         Pin Attributes Query Interface</b>
 *
 * @b Description: Provides a mechanism for ODMs to specify electrical
 *                 attributes, such as drive strength, for pins.
 */

#ifndef INCLUDED_NVODM_QUERY_PINS_H
#define INCLUDED_NVODM_QUERY_PINS_H

/**
 * @defgroup nvodm_pins Pin Electrical Attributes Query Interface
 * This is the ODM query interface for pin electrical attributes.
 *
 * Pin attribute settings match the hardware register definitions very
 * closely, and as such are specified in a chip-specific format.  C-language
 * pre-processor macros are provided to allow for as much code readability
 * and maintainability as possible. Because the organization and the
 * electrical fine-tuning capabilities of the pins may change between
 * products, ODMs should ensure that they are using the macros that match
 * the SOC in their product.
 * @ingroup nvodm_query
 * @{
 */

#include "nvcommon.h"

#if defined(__cplusplus)
extern "C"
{
#endif


/**
 * Defines the pin attributes record.
 */
typedef struct NvOdmPinAttribRec
{
    ///  Specifies the configuration register to assign, which should be
    ///  one of the application processor's NvOdmPinRegister enumerants.
    NvU32 ConfigRegister;

    ///  Specifies the value to assign to the specified configuration register.
    ///  Each application processor's header file provides pre-processor
    ///  macros to assist in defining this value.
    NvU32 Value;
} NvOdmPinAttrib;

/**
 * Gets a list of [configuration register, value] pairs that are applied
 * to the application processor's pin configuration registers. Any
 * pin configuration register that is not specified in this list is left at
 * its current state.
 *
 * @param pPinAttributes A returned pointer to an array of constant pin
 *  configuration attributes, or NULL if no pin configuration registers
 *  should be programmed.
 *
 * @return The number of pin configuration attributes in \a pPinAttributes, or
 *  0 if none.
 */

NvU32
NvOdmQueryPinAttributes(const NvOdmPinAttrib **pPinAttributes);

#if defined(__cplusplus)
}
#endif

/** @} */
#endif

