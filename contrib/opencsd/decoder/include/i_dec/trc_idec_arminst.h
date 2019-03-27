/*
 * \file       trc_idec_arminst.h
 * \brief      OpenCSD : 
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

#ifndef ARM_TRC_IDEC_ARMINST_H_INCLUDED
#define ARM_TRC_IDEC_ARMINST_H_INCLUDED

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS 1
#endif

#include "opencsd/ocsd_if_types.h"
#include <cstdint>

/*
For Thumb2, test if a halfword is the first half of a 32-bit instruction,
as opposed to a complete 16-bit instruction.
*/
inline int is_wide_thumb(uint16_t insthw)
{
    return (insthw & 0xF800) >= 0xE800;
}

/*
In the following queries, 16-bit Thumb2 instructions should be
passed in as the high halfword, e.g. xxxx0000.
*/

/*
Test whether an instruction is a branch (software change of the PC).
This includes branch instructions and all loads and data-processing 
instructions that write to the PC.  It does not include exception
instructions such as SVC, HVC and SMC.
(Performance event 0x0C includes these.)
*/
int inst_ARM_is_branch(uint32_t inst);
int inst_Thumb_is_branch(uint32_t inst);
int inst_A64_is_branch(uint32_t inst);

/*
Test whether an instruction is a direct (aka immediate) branch.
Performance event 0x0D counts these.
*/
int inst_ARM_is_direct_branch(uint32_t inst);
int inst_Thumb_is_direct_branch(uint32_t inst);
int inst_A64_is_direct_branch(uint32_t inst);

/*
Get branch destination for a direct branch.
*/
int inst_ARM_branch_destination(uint32_t addr, uint32_t inst, uint32_t *pnpc);
int inst_Thumb_branch_destination(uint32_t addr, uint32_t inst, uint32_t *pnpc);
int inst_A64_branch_destination(uint64_t addr, uint32_t inst, uint64_t *pnpc);

int inst_ARM_is_indirect_branch(uint32_t inst);
int inst_Thumb_is_indirect_branch(uint32_t inst);
int inst_A64_is_indirect_branch(uint32_t inst);

int inst_ARM_is_branch_and_link(uint32_t inst);
int inst_Thumb_is_branch_and_link(uint32_t inst);
int inst_A64_is_branch_and_link(uint32_t inst);

int inst_ARM_is_conditional(uint32_t inst);
int inst_Thumb_is_conditional(uint32_t inst);
int inst_A64_is_conditional(uint32_t inst);

/* For an IT instruction, return the number of instructions conditionalized
   (from 1 to 4).  For other instructions, return zero. */
unsigned int inst_Thumb_is_IT(uint32_t inst);

typedef enum {
    ARM_BARRIER_NONE,
    ARM_BARRIER_ISB,
    ARM_BARRIER_DMB,
    ARM_BARRIER_DSB
} arm_barrier_t;

arm_barrier_t inst_ARM_barrier(uint32_t inst);
arm_barrier_t inst_Thumb_barrier(uint32_t inst);
arm_barrier_t inst_A64_barrier(uint32_t inst);

/*
Test whether an instruction is definitely undefined, e.g. because
allocated to a "permanently UNDEFINED" space (UDF mnemonic).
Other instructions besides the ones indicated, may always or
sometimes cause an undefined instruction trap.  This call is
intended to be helpful in 'runaway decode' prevention.
*/
int inst_ARM_is_UDF(uint32_t inst);
int inst_Thumb_is_UDF(uint32_t inst);
int inst_A64_is_UDF(uint32_t inst);


/* access sub-type information */
ocsd_instr_subtype get_instr_subtype();
void clear_instr_subtype();

#endif // ARM_TRC_IDEC_ARMINST_H_INCLUDED

/* End of File trc_idec_arminst.h */
