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
 *         KBC Adaptation Interface</b>
 *
 * @b Description: Defines the ODM adaptation interface for KBC keypad.
 * 
 */

#ifndef INCLUDED_NVODM_KBC_H
#define INCLUDED_NVODM_KBC_H

#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvodm_services.h"

/**
 * @defgroup nvodm_kbc Keyboard Controller Adaptation Interface
 * This is the keyboard controller (KBC) ODM adaptation interface.
 * See also the \link nvodm_query_kbc ODM Query KBC Interface\endlink.
 * @ingroup nvodm_adaptation
 * @{
 */
/**
 * This API takes the keys that have been pressed as input and filters out the
 * the keys that may have been caused due to ghosting effect and key roll-over.
 *
 * @note The row and column numbers of the keys that have been left after the 
 * filtering are stored in the \a pRows and \a pCols arrays. The extra keys must be 
 * deleted from the array.
 *
 * @param pRows A pointer to the array of the row numbers of the keys that have 
 * been detected. This array contains \a NumOfKeysPressed elements.
 *
 * @param pCols A pointer to the array of the column numbers of the keys that have 
 * been detected. This array contains \a NumOfKeysPressed elements. 
 * 
 * @param NumOfKeysPressed The number of key presses that have been detected by 
 * the driver.
 *
 * @return The number of keys pressed after the filter has been applied.
 *
*/
NvU32
NvOdmKbcFilterKeys(
    NvU32 *pRows,
    NvU32 *pCols,
    NvU32 NumOfKeysPressed);


#if defined(__cplusplus)
    }
#endif
    
/** @} */
    
#endif // INCLUDED_NVODM_KBC_H

