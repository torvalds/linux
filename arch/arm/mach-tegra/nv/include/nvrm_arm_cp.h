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


#ifndef INCLUDED_ARM_CP_H
#define INCLUDED_ARM_CP_H

#include "nvassert.h"

#ifdef  __cplusplus
extern "C" {
#endif

//==========================================================================
// Compiler-specific status and coprocessor register abstraction macros.
//==========================================================================

#if defined(_MSC_VER) && NVOS_IS_WINDOWS_CE // Microsoft compiler on WinCE

    // Define the standard ARM coprocessor register names because the ARM compiler requires
    // that we use the names and the Microsoft compiler requires that we use the numbers for
    // its intrinsic functions _MoveToCoprocessor() and _MoveFromCoprocessor().
    #define p14     14
    #define p15     15
    #define c0      0
    #define c1      1
    #define c2      2
    #define c3      3
    #define c4      4
    #define c5      5
    #define c6      6
    #define c7      7
    #define c8      8
    #define c9      9
    #define c10     10
    #define c11     11
    #define c12     12
    #define c13     13
    #define c14     14
    #define c15     15

    /*
     *  @brief Macro to abstract writing of a ARM coprocessor register via the MCR instruction.
     *  @param cp is the coprocessor name (e.g., p15)
     *  @param op1 is a coprocessor-specific operation code (must be a manifest constant).
     *  @param Rd is a variable that will receive the value read from the coprocessor register.
     *  @param CRn is the destination coprocessor register (e.g., c7).
     *  @param CRm is an additional destination coprocessor register (e.g., c2).
     *  @param op2 is a coprocessor-specific operation code (must be a manifest constant).
    */
    #define MCR(cp,op1,Rd,CRn,CRm,op2)  _MoveToCoprocessor((NvU32)(Rd), cp, op1, CRn, CRm, op2)

    /*
     *  @brief Macro to abstract reading of a ARM coprocessor register via the MRC instruction.
     *  @param cp is the coprocessor name (e.g., p15)
     *  @param op1 is a coprocessor-specific operation code (must be a manifest constant).
     *  @param Rd is a variable that will receive the value read from the coprocessor register.
     *  @param CRn is the destination coprocessor register (e.g., c7).
     *  @param CRm is an additional destination coprocessor register (e.g., c2).
     *  @param op2 is a coprocessor-specific operation code (must be a manifest constant).
    */
    #define MRC(cp,op1,Rd,CRn,CRm,op2)  *((NvU32*)(&(Rd))) = _MoveFromCoprocessor(cp, op1, CRn, CRm, op2)

#elif defined(__ARMCC_VERSION)  // ARM compiler

    /*
     *  @brief Macro to abstract writing of a ARM coprocessor register via the MCR instruction.
     *  @param cp is the coprocessor name (e.g., p15)
     *  @param op1 is a coprocessor-specific operation code (must be a manifest constant).
     *  @param Rd is a variable that will be written to the coprocessor register.
     *  @param CRn is the destination coprocessor register (e.g., c7)
     *  @param CRm is an additional destination coprocessor register (e.g., c2).
     *  @param op2 is a coprocessor-specific operation code (must be a manifest constant).
    */
    #define MCR(cp,op1,Rd,CRn,CRm,op2)  __asm { MCR cp, op1, Rd, CRn, CRm, op2 }

    /*
     *  @brief Macro to abstract reading of a ARM coprocessor register via the MRC instruction.
     *  @param cp is the coprocessor name (e.g., p15)
     *  @param op1 is a coprocessor-specific operation code (must be a manifest constant).
     *  @param Rd is a variable that will receive the value read from the coprocessor register.
     *  @param CRn is the destination coprocessor register (e.g., c7).
     *  @param CRm is an additional destination coprocessor register (e.g., c2).
     *  @param op2 is a coprocessor-specific operation code (must be a manifest constant).
    */
    #define MRC(cp,op1,Rd,CRn,CRm,op2)  __asm { MRC cp, op1, Rd, CRn, CRm, op2 }

#elif NVOS_IS_LINUX || __GNUC__ // linux compilers

    #if defined(__arm__)    // ARM GNU compiler

    // Define the standard ARM coprocessor register names because the ARM compiler requires
    // that we use the names and the GNU compiler requires that we use the numbers.
    #define p14     14
    #define p15     15
    #define c0      0
    #define c1      1
    #define c2      2
    #define c3      3
    #define c4      4
    #define c5      5
    #define c6      6
    #define c7      7
    #define c8      8
    #define c9      9
    #define c10     10
    #define c11     11
    #define c12     12
    #define c13     13
    #define c14     14
    #define c15     15

    /*
     *  @brief Macro to abstract writing of a ARM coprocessor register via the MCR instruction.
     *  @param cp is the coprocessor name (e.g., p15)
     *  @param op1 is a coprocessor-specific operation code (must be a manifest constant).
     *  @param Rd is a variable that will receive the value read from the coprocessor register.
     *  @param CRn is the destination coprocessor register (e.g., c7).
     *  @param CRm is an additional destination coprocessor register (e.g., c2).
     *  @param op2 is a coprocessor-specific operation code (must be a manifest constant).
    */
    #define MCR(cp,op1,Rd,CRn,CRm,op2)  asm(" MCR " #cp",%1,%2,"#CRn","#CRm ",%5" \
        : : "i" (cp), "i" (op1), "r" (Rd), "i" (CRn), "i" (CRm), "i" (op2))

    /*
     *  @brief Macro to abstract reading of a ARM coprocessor register via the MRC instruction.
     *  @param cp is the coprocessor name (e.g., p15)
     *  @param op1 is a coprocessor-specific operation code (must be a manifest constant).
     *  @param Rd is a variable that will receive the value read from the coprocessor register.
     *  @param CRn is the destination coprocessor register (e.g., c7).
     *  @param CRm is an additional destination coprocessor register (e.g., c2).
     *  @param op2 is a coprocessor-specific operation code (must be a manifest constant).
    */
    #define MRC(cp,op1,Rd,CRn,CRm,op2)  asm( " MRC " #cp",%2,%0," #CRn","#CRm",%5" \
        : "=r" (Rd) : "i" (cp), "i" (op1), "i" (CRn), "i" (CRm), "i" (op2))

    #else
    
    /* x86 processor. No such instructions. Callers should not call these macros
     * when running on x86. If they do, it will compile but will not work. */
    #define MCR(cp,op1,Rd,CRn,CRm,op2)  do { Rd = Rd; NV_ASSERT(0); } while (0)
    #define MRC(cp,op1,Rd,CRn,CRm,op2)  do { Rd = 0; /*NV_ASSERT(0);*/ } while (0)

    #endif
#else

    // !!!FIXME!!! TEST FOR ALL KNOWN COMPILERS -- FOR NOW JUST DIE AT RUN-TIME
    // #error "Unknown compiler"
    #define MCR(cp,op1,Rd,CRn,CRm,op2)  do { Rd = Rd; NV_ASSERT(0); } while (0)
    #define MRC(cp,op1,Rd,CRn,CRm,op2)  do { Rd = 0; /*NV_ASSERT(0);*/ } while (0)

#endif


#ifdef  __cplusplus
}
#endif

#endif // INCLUDED_ARM_CP_H

