/*
 * Copyright (c) 2006-2009 NVIDIA Corporation.
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
 *         Keyboard Controller Interface</b>
 *
 * @b Description: Defines the ODM query interface for NVIDIA keyboard 
 *                 controller (KBC) adaptation.
 * 
 */

#ifndef INCLUDED_NVODM_QUERY_KBC_H
#define INCLUDED_NVODM_QUERY_KBC_H

#include "nvcommon.h"

/**
 * @defgroup nvodm_query_kbc Keyboard Controller Query Interface
 * This is the keyboard controller (KBC) ODM Query interface.
 * See also the \link nvodm_kbc KBC ODM Adaptation Interface\endlink.
 * @ingroup nvodm_query
 * @{
 */

/**
 * Defines the parameters associated with this device.
 */
typedef enum
{
    NvOdmKbcParameter_NumOfRows=1,
    NvOdmKbcParameter_NumOfColumns,
    NvOdmKbcParameter_DebounceTime,
    NvOdmKbcParameter_RepeatCycleTime,
    NvOdmKbcParameter_Force32 = 0x7FFFFFFF
} NvOdmKbcParameter;

/**
 * Queries the peripheral device for its current settings.
 * 
 * @see NvOdmKbcParameter
 *
 * @param param  Specifies which parameter value to get.
 * @param sizeOfValue  The length of the parameter data (in bytes).
 * @param value  A pointer to the location where the requested parameter shall
 *     be stored.
 * 
 */
void 
NvOdmKbcGetParameter(
        NvOdmKbcParameter param,
        NvU32 sizeOfValue,
        void *value);

/**
 * Gets the key code depending upon the row and column values.
 * 
 * @param Row  The value of the row.
 * @param Column The value of the column.
 * @param RowCount The number of the rows present in the keypad matrix.
 * @param ColumnCount The number of the columns present in the keypad matrix.
 * 
 * @return The appropriate key code.
 */
NvU32 
NvOdmKbcGetKeyCode(
    NvU32 Row, 
    NvU32 Column,
    NvU32 RowCount,
    NvU32 ColumnCount);

/**
 * Queries if wake-up only on selected keys is enabled for WPC-like
 * configurations. If it is enabled, returns the pointers to the static array
 * containing the row and columns numbers. If this is enabled and \a NumOfKeys
 * selected is zero, all the keys are disabled for wake-up when system is
 * suspended.
 * 
 * @note The selected keys must not be a configuration of type 1x1, 1x2, etc. 
 * In other words, a minimum of two rows must be enabled due to hardware
 * limitations.
 *
 * @param pRowNumber A pointer to the static array containing the row 
 *                   numbers of the keys.
 * @param pColNumber A pointer to the static array containing the column 
 *                   numbers of the keys.
 * @param NumOfKeys A pointer to the number of keys that must be enabled.
 *                  This indicates the number of elements in the arrays pointer by
 *                   \a pRowNumber and \a pColNumber.
 * @return NV_TRUE if successful, or NV_FALSE otherwise.
 */
NvBool
NvOdmKbcIsSelectKeysWkUpEnabled(
    NvU32 **pRowNumber,
    NvU32 **pColNumber,
    NvU32 *NumOfKeys);

/** @} */
#endif // INCLUDED_NVODM_QUERY_KBC_H

