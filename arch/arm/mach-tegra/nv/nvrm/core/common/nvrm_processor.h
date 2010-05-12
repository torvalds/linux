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

#ifndef INCLUDED_NVRM_PROCESSOR_H
#define INCLUDED_NVRM_PROCESSOR_H

#include "nvcommon.h"

#ifdef  __cplusplus
extern "C" {
#endif


//==========================================================================
//  ARM CPSR/SPSR definitions
//==========================================================================

#define PSR_MODE_MASK   0x1F
#define PSR_MODE_USR    0x10
#define PSR_MODE_FIQ    0x11
#define PSR_MODE_IRQ    0x12
#define PSR_MODE_SVC    0x13
#define PSR_MODE_ABT    0x17
#define PSR_MODE_UND    0x1B
#define PSR_MODE_SYS    0x1F    // only available on ARM Arch. v4 and higher
#define PSR_MODE_MON    0x16    // only available on ARM Arch. v6 and higher with TrustZone extension


//==========================================================================
// Compiler-independent abstraction macros.
//==========================================================================

#define IS_USER_MODE(cpsr)  ((cpsr & PSR_MODE_MASK) == PSR_MODE_USR)

//==========================================================================
// Compiler-specific instruction abstraction macros.
//==========================================================================

#if defined(__arm__) && !defined(__thumb__)  // ARM compiler compiling ARM code

    #if (__GNUC__) // GCC inline assembly syntax

    static NV_INLINE NvU32
    CountLeadingZeros(NvU32 x)
    {
        NvU32 count;
        __asm__ __volatile__ (      \
                "clz %0, %1 \r\t"   \
                :"=r"(count)        \
                :"r"(x));
        return count;
    }

    #define GET_CPSR(x) __asm__ __volatile__ (          \
                                "mrs %0, cpsr\r\t"     \
                                : "=r"(x))

    #else   // assume RVDS compiler
    /*
     *  @brief Macro to abstract retrieval of the current processor 
     *  status register (CPSR) value.
     *  @param x is a variable of type NvU32 that will receive 
     *  the CPSR value.
     */
    #define GET_CPSR(x) __asm { MRS x, CPSR }           // x = CPSR
    
    static NV_INLINE NvU32
    CountLeadingZeros(NvU32 x)
    {
        NvU32 count;
        __asm { CLZ count, x }
        return count;
    }

    #endif
#else
    /*
     *  @brief Macro to abstract retrieval of the current processor status register (CPSR) value.
     *  @param x is a variable of type NvU32 that will receive the CPSR value.
    */
    #define GET_CPSR(x) (x = PSR_MODE_USR)  // Always assume USER mode for now

    // If no built-in method for counting leading zeros do it the less efficient way.
    static NV_INLINE NvU32 
    CountLeadingZeros(NvU32 x)
    {
        NvU32   i;

        if (x)
        {
            i = 0;

            do
            {
                if (x & 0x80000000)
                {
                    break;
                }
                x <<= 1;
            } while (++i < 32);
        }
        else
        {
            i = 32;
        }

        return i;
    }

#endif

#ifdef  __cplusplus
}
#endif

#endif // INCLUDED_NVRM_PROCESSOR_H
