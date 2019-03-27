/*
 * \file       trc_i_decode.cpp
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

#include "opencsd/ocsd_if_types.h"
#include "i_dec/trc_i_decode.h"
#include "i_dec/trc_idec_arminst.h"

ocsd_err_t TrcIDecode::DecodeInstruction(ocsd_instr_info *instr_info)
{
    ocsd_err_t err = OCSD_OK;
    clear_instr_subtype();
    switch(instr_info->isa)
    {
    case ocsd_isa_arm:
        err = DecodeA32(instr_info);
        break;

    case ocsd_isa_thumb2:
        err = DecodeT32(instr_info);
        break;

    case ocsd_isa_aarch64:
        err = DecodeA64(instr_info);
        break;

    case ocsd_isa_tee:    
    case ocsd_isa_jazelle:
    default:
        // unsupported ISA
        err = OCSD_ERR_UNSUPPORTED_ISA;
        break;
    }
    instr_info->sub_type = get_instr_subtype();
    return err;
}

ocsd_err_t TrcIDecode::DecodeA32(ocsd_instr_info *instr_info)
{
    uint32_t branchAddr = 0;
    arm_barrier_t barrier;

    instr_info->instr_size = 4; // instruction size A32
    instr_info->type =  OCSD_INSTR_OTHER;  // default type
    instr_info->next_isa = instr_info->isa; // assume same ISA 
    instr_info->is_link = 0;

    if(inst_ARM_is_indirect_branch(instr_info->opcode))
    {
        instr_info->type = OCSD_INSTR_BR_INDIRECT;
        instr_info->is_link = inst_ARM_is_branch_and_link(instr_info->opcode);
    }
    else if(inst_ARM_is_direct_branch(instr_info->opcode))
    {
        inst_ARM_branch_destination((uint32_t)instr_info->instr_addr,instr_info->opcode,&branchAddr);
        instr_info->type = OCSD_INSTR_BR;
        if (branchAddr & 0x1)
        {
            instr_info->next_isa = ocsd_isa_thumb2;
            branchAddr &= ~0x1;
        }
        instr_info->branch_addr = (ocsd_vaddr_t)branchAddr;
        instr_info->is_link = inst_ARM_is_branch_and_link(instr_info->opcode);
    }
    else if((barrier = inst_ARM_barrier(instr_info->opcode)) != ARM_BARRIER_NONE)
    {
        switch(barrier)
        {
        case ARM_BARRIER_ISB: 
            instr_info->type = OCSD_INSTR_ISB;             
            break;

        case ARM_BARRIER_DSB:
        case ARM_BARRIER_DMB:
            if(instr_info->dsb_dmb_waypoints)
                instr_info->type = OCSD_INSTR_DSB_DMB;             
            break;
        }
    }

    instr_info->is_conditional = inst_ARM_is_conditional(instr_info->opcode);

    return OCSD_OK;
}

ocsd_err_t TrcIDecode::DecodeA64(ocsd_instr_info *instr_info)
{
    uint64_t branchAddr = 0;
    arm_barrier_t barrier;

    instr_info->instr_size =  4; // default address update
    instr_info->type =  OCSD_INSTR_OTHER;  // default type
    instr_info->next_isa = instr_info->isa; // assume same ISA 
    instr_info->is_link = 0;
    
    if(inst_A64_is_indirect_branch(instr_info->opcode))
    {
        instr_info->type = OCSD_INSTR_BR_INDIRECT;
        instr_info->is_link = inst_A64_is_branch_and_link(instr_info->opcode);
    }
    else if(inst_A64_is_direct_branch(instr_info->opcode))
    {
        inst_A64_branch_destination(instr_info->instr_addr,instr_info->opcode,&branchAddr);
        instr_info->type = OCSD_INSTR_BR;
        instr_info->branch_addr = (ocsd_vaddr_t)branchAddr;
        instr_info->is_link = inst_A64_is_branch_and_link(instr_info->opcode);
    }
    else if((barrier = inst_A64_barrier(instr_info->opcode)) != ARM_BARRIER_NONE)
    {
        switch(barrier)
        {
        case ARM_BARRIER_ISB: 
            instr_info->type = OCSD_INSTR_ISB;             
            break;

        case ARM_BARRIER_DSB:
        case ARM_BARRIER_DMB:
            if(instr_info->dsb_dmb_waypoints)
                instr_info->type = OCSD_INSTR_DSB_DMB;             
            break;
        }
    }

    instr_info->is_conditional = inst_A64_is_conditional(instr_info->opcode);

    return OCSD_OK;
}

ocsd_err_t TrcIDecode::DecodeT32(ocsd_instr_info *instr_info)
{
    uint32_t branchAddr = 0;
    arm_barrier_t barrier;

    // need to align the 32 bit opcode as 2 16 bit, with LS 16 as in top 16 bit of 
    // 32 bit word - T2 routines assume 16 bit in top 16 bit of 32 bit opcode.
    uint32_t op_temp = (instr_info->opcode >> 16) & 0xFFFF;
    op_temp |= ((instr_info->opcode & 0xFFFF) << 16);
    instr_info->opcode = op_temp;


    instr_info->instr_size = is_wide_thumb((uint16_t)(instr_info->opcode >> 16)) ? 4 : 2;
    instr_info->type =  OCSD_INSTR_OTHER;  // default type
    instr_info->next_isa = instr_info->isa; // assume same ISA 
    instr_info->is_link = 0;

    if(inst_Thumb_is_indirect_branch(instr_info->opcode))
    {
        instr_info->type = OCSD_INSTR_BR_INDIRECT;
        instr_info->is_link = inst_Thumb_is_branch_and_link(instr_info->opcode);
    }
    else if(inst_Thumb_is_direct_branch(instr_info->opcode))
    {
        inst_Thumb_branch_destination((uint32_t)instr_info->instr_addr,instr_info->opcode,&branchAddr);
        instr_info->type = OCSD_INSTR_BR;
        instr_info->branch_addr = (ocsd_vaddr_t)(branchAddr & ~0x1);
        if((branchAddr & 0x1) == 0)
            instr_info->next_isa = ocsd_isa_arm;
        instr_info->is_link = inst_Thumb_is_branch_and_link(instr_info->opcode);
    }
    else if((barrier = inst_Thumb_barrier(instr_info->opcode)) != ARM_BARRIER_NONE)
    {
        switch(barrier)
        {
        case ARM_BARRIER_ISB: 
            instr_info->type = OCSD_INSTR_ISB;             
            break;

        case ARM_BARRIER_DSB:
        case ARM_BARRIER_DMB:
            if(instr_info->dsb_dmb_waypoints)
                instr_info->type = OCSD_INSTR_DSB_DMB;             
            break;
        }
    }

    instr_info->is_conditional = inst_Thumb_is_conditional(instr_info->opcode);
    instr_info->thumb_it_conditions = inst_Thumb_is_IT(instr_info->opcode);

    return OCSD_OK;
}


/* End of File trc_i_decode.cpp */
