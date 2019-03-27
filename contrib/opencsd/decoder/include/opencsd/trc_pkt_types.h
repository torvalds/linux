/*!
 * \file       opencsd/trc_pkt_types.h
 * \brief      OpenCSD: Common "C" types for trace packets.
 * 
 * \copyright  Copyright (c) 2015, ARM Limited. All Rights Reserved.
 */


/* 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * 
 * 3. Neither the name of the copyright holder nor the names of its contributors 
 * may be used to endorse or promote products derived from this software without 
 * specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 'AS IS' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND 
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */ 

#ifndef ARM_TRC_PKT_TYPES_H_INCLUDED
#define ARM_TRC_PKT_TYPES_H_INCLUDED

#include <stdint.h>
#include "opencsd/ocsd_if_types.h"

/** @defgroup trc_pkts  OpenCSD Library : Trace Packet Types

    @brief Types used in trace packet description structures.

@{*/


/** @name Common Packet Types
@{*/

typedef enum _ocsd_pkt_va_size
{
    VA_32BIT,
    VA_64BIT
} ocsd_pkt_va_size;

typedef struct _ocsd_pkt_vaddr 
{
    ocsd_pkt_va_size size;     /**< Virtual address size. */
    ocsd_vaddr_t val;  /**< Current value */
    uint8_t pkt_bits;   /**< Bits updated this packet */
    uint8_t valid_bits; /**< Currently valid bits */
} ocsd_pkt_vaddr;

typedef struct _ocsd_pkt_byte_sz_val
{
    uint32_t val;
    uint8_t  size_bytes;
    uint8_t  valid_bytes;
} ocsd_pkt_byte_sz_val;

typedef enum _ocsd_pkt_atm_type
{
    ATOM_PATTERN,   /**< set atom packet using pattern supplied */
    ATOM_REPEAT     /**< set atom packet using repeat value (convert to pattern) */
} ocsd_pkt_atm_type;

typedef enum _ocsd_atm_val {
    ATOM_N,
    ATOM_E
} ocsd_atm_val;

typedef struct _ocsd_pkt_atom
{
    /** pattern across num bits.
        Bit sequence:- ls bit = oldest atom (1st instruction executed), ms bit = newest (last instruction executed), 
        Bit values  :-  1'b1 = E atom, 1'b0 = N atom.
      */
    uint32_t En_bits;
    uint8_t num;                /**< number of atoms represented */
} ocsd_pkt_atom;

/** Isync Reason - common to PTM and ETMv3 **/
typedef enum _ocsd_iSync_reason {
    iSync_Periodic = 0,
    iSync_TraceEnable,
    iSync_TraceRestartAfterOverflow,
    iSync_DebugExit
} ocsd_iSync_reason;


typedef enum _ocsd_armv7_exception {
    Excp_Reserved,
    Excp_NoException,
    Excp_Reset,
    Excp_IRQ, 
    Excp_FIQ,
    Excp_AsyncDAbort,
    Excp_DebugHalt,
    Excp_Jazelle,
    Excp_SVC,
    Excp_SMC,
    Excp_Hyp,
    Excp_Undef,
    Excp_PrefAbort,
    Excp_Generic,
    Excp_SyncDataAbort,
    Excp_CMUsageFault,
    Excp_CMNMI,
    Excp_CMDebugMonitor,
    Excp_CMMemManage,
    Excp_CMPendSV,
    Excp_CMSysTick,
    Excp_CMBusFault,
    Excp_CMHardFault,
    Excp_CMIRQn,
    Excp_ThumbEECheckFail,
} ocsd_armv7_exception;

/** @}*/

/** @}*/

#endif // ARM_TRC_PKT_TYPES_H_INCLUDED

/* End of File opencsd/trc_pkt_types.h */
