/*
 * \file       ocsd_code_follower.cpp
 * \brief      OpenCSD : Instruction Code path follower.
 * 
 * \copyright  Copyright (c) 2016, ARM Limited. All Rights Reserved.
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

#include "common/ocsd_code_follower.h"

OcsdCodeFollower::OcsdCodeFollower()
{
    m_instr_info.pe_type.arch = ARCH_UNKNOWN;
    m_instr_info.pe_type.profile = profile_Unknown;
    m_instr_info.isa = ocsd_isa_unknown;
    m_instr_info.dsb_dmb_waypoints = 0;
    m_instr_info.instr_addr = 0;
    m_instr_info.opcode = 0;
    m_pMemAccess = 0;
    m_pIDecode = 0;
    m_mem_space_csid = 0;
    m_st_range_addr =  m_en_range_addr = m_next_addr = 0;
    m_b_next_valid = false;
    m_b_nacc_err = false;
}

OcsdCodeFollower::~OcsdCodeFollower()
{
}

void OcsdCodeFollower::initInterfaces(componentAttachPt<ITargetMemAccess> *pMemAccess, componentAttachPt<IInstrDecode> *pIDecode)
{
    m_pMemAccess = pMemAccess;
    m_pIDecode = pIDecode;
}

bool OcsdCodeFollower::initFollowerState()
{
    bool initDone = false;

    // reset per follow flags
    m_b_next_valid = false;
    m_b_nacc_err = false;

    // set range addresses
    m_en_range_addr = m_next_addr = m_st_range_addr;

// check initialisation is valid.

    // must have attached memory access and i-decode objects
    if(m_pMemAccess && m_pIDecode)
    {
        initDone = (m_pMemAccess->hasAttachedAndEnabled() && m_pIDecode->hasAttachedAndEnabled());
    }
    return initDone;
}

/*!
 * Decodes an instruction at a single location, calculates the next address 
 * if possible according to the instruction type and atom.
 *
 * @param addrStart : Address of the instruction        
 * @param A :  Atom value - E or N
 *
 * @return ocsd_err_t : OCSD_OK - decode correct, check flags for next address
 *                    : OCSD_ERR_MEM_NACC - unable to access memory area @ address - need new address in trace packet stream.
 *                    : OCSD_ERR_NOT_INIT - not initialised - fatal.
 *                    : OCSD_<other>  - other error occured - fatal.
 */
ocsd_err_t OcsdCodeFollower::followSingleAtom(const ocsd_vaddr_t addrStart, const ocsd_atm_val A)
{
    ocsd_err_t err = OCSD_ERR_NOT_INIT;

    if(!initFollowerState())
        return err;

    m_en_range_addr = m_st_range_addr = m_instr_info.instr_addr = addrStart;
    err = decodeSingleOpCode();
   
    if(err != OCSD_OK)
        return err;

    // set end range - always after the instruction executed.
    m_en_range_addr = m_instr_info.instr_addr + m_instr_info.instr_size;
                    
    // assume next addr is the instruction after
    m_next_addr = m_en_range_addr;
    m_b_next_valid = true;

    // case when next address is different
    switch(m_instr_info.type)                        
    {
    case OCSD_INSTR_BR:
        if(A == ATOM_E) // executed the direct branch
            m_next_addr = m_instr_info.branch_addr;
        break;

    case OCSD_INSTR_BR_INDIRECT:
        if(A == ATOM_E) // executed indirect branch
            m_b_next_valid = false;
        break;
    }
    return err;
}

ocsd_err_t OcsdCodeFollower::decodeSingleOpCode()
{
    ocsd_err_t err = OCSD_OK;
    // request 4 bytes for the opcode - even for Thumb which may be T32
    uint32_t bytesReq = 4;  
    uint32_t opcode;    // buffer for opcode

    // read memory location for opcode 
    err = m_pMemAccess->first()->ReadTargetMemory(m_instr_info.instr_addr,m_mem_space_csid,m_mem_acc_rule,&bytesReq,(uint8_t *)&opcode);

    // operational error (not access problem - that is indicated by 0 bytes returned)
    if(err != OCSD_OK)
        return err;

    if(bytesReq == 4)       // check that we got all memory requested.
    {
        m_instr_info.opcode = opcode;
        err = m_pIDecode->first()->DecodeInstruction(&m_instr_info);
    }
    else       // otherwise memory unavailable.
    {
        m_b_nacc_err = true;
        m_nacc_address = m_instr_info.instr_addr;
        err = OCSD_ERR_MEM_NACC;
    }
    return err;
}

/* End of File ocsd_code_follower.cpp */
