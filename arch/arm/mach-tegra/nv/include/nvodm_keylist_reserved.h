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
 *         Reserved Key ID Definition</b>
 *
 * @b Description: Defines the reserved key IDs for the default keys provided
 *                 by the ODM key/value list service.
 */

#ifndef INCLUDED_NVODM_KEYLIST_RESERVED_H
#define INCLUDED_NVODM_KEYLIST_RESERVED_H

/**
 * @addtogroup nvodm_services
 * @{
 */

#if defined(__cplusplus)
extern "C"
{
#endif

/**
 * Defines the list of reserved key IDs for the ODM key/value list service.
 * These keys may be read by calling NvOdmServicesGetKeyValue(), but they
 * may not be modified.
 */
enum 
{
    /// Specifies the starting range of key IDs reserved for use by NVIDIA.
    NvOdmKeyListId_ReservedAreaStart = 0x6fff0000UL,

    /** Returns the value stored in the CustomerOption field of the BCT,
     *  which was specified when the device was flashed. If no value was
     *  specified when flashing, a default value of 0 will be returned. */
    NvOdmKeyListId_ReservedBctCustomerOption = NvOdmKeyListId_ReservedAreaStart,

    /// Specifes the last ID of the reserved key area.
    NvOdmKeyListId_ReservedAreaEnd = 0x6ffffffeUL

};

#if defined(__cplusplus)
}
#endif

/** @} */
#endif
