/*
 * \file       ocsd_code_follower.h
 * \brief      OpenCSD : Code follower for instruction trace decode
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

#ifndef ARM_OCSD_CODE_FOLLOWER_H_INCLUDED
#define ARM_OCSD_CODE_FOLLOWER_H_INCLUDED

#include "opencsd/ocsd_if_types.h"
#include "opencsd/trc_pkt_types.h"
#include "comp_attach_pt_t.h"
#include "interfaces/trc_tgt_mem_access_i.h"
#include "interfaces/trc_instr_decode_i.h"

/*!
 * @class OcsdCodeFollower
 * @brief The code follower looks for waypoints or addresses. 
 * 
 *  Code follower used to determine the trace ranges for Atom or other waypoint
 *  elements. Uses memory accessor and I decoder to follow the code path.
 * 
 */
class OcsdCodeFollower
{
public:
    OcsdCodeFollower();
    ~OcsdCodeFollower();

//*********** setup API
    void initInterfaces(componentAttachPt<ITargetMemAccess> *pMemAccess, componentAttachPt<IInstrDecode> *pIDecode);
    
// set information for decode operation - static or occasionally changing settings
// per decode values are passed as parameters into the decode API calls.
    void setArchProfile(const ocsd_arch_profile_t profile);             //!< core profile
    void setMemSpaceAccess(const ocsd_mem_space_acc_t mem_acc_rule);    //!< memory space to use for access (filtered by S/NS, EL etc).
    void setMemSpaceCSID(const uint8_t csid);                           //!< memory spaces might be partitioned by CSID
    void setISA(const ocsd_isa isa);    //!< set the ISA for the decode.
    void setDSBDMBasWP();   //!< DSB and DMB can be treated as WP in some archs.

//********** code following API

    // standard WP search - for program flow trace
    //ocsd_err_t followToAtomWP(idec_res_t &op_result, const ocsd_vaddr_t addrStart, const ocsd_atm_val A);

    // PTM exception code may require follow to an address
    //ocsd_err_t followToAddress(idec_res_t &op_result, const ocsd_vaddr_t addrStart, const ocsd_atm_val A, const ocsd_vaddr_t addrMatch);

    // single instruction atom format such as ETMv3
    ocsd_err_t followSingleAtom(const ocsd_vaddr_t addrStart, const ocsd_atm_val A);

    // follow N instructions
    // ocsd_err_t followNInstructions(idec_res_t &op_result) // ETMv4 Q elements

//*********************** results API
    const ocsd_vaddr_t getRangeSt() const;  //!< inclusive start address of decoded range (value passed in)
    const ocsd_vaddr_t getRangeEn() const;  //!< exclusive end address of decoded range (first instruction _not_ executed / potential next instruction).
    const bool hasRange() const;            //!< we have a valid range executed (may be false if nacc).

    const bool hasNextAddr() const;         //!< we have calulated the next address - otherwise this is needed from trace packets.
    const ocsd_vaddr_t getNextAddr() const; //!< next address - valid if hasNextAddr() true.

    // information on last instruction executed in range.
    const ocsd_instr_type getInstrType() const;         //!< last instruction type
    const ocsd_instr_subtype getInstrSubType() const;   //!< last instruction sub-type
    const bool isCondInstr() const;                     //!< is a conditional instruction
    const bool isLink() const;                          //!< is a link (branch with link etc)
    const bool ISAChanged() const;                      //!< next ISA different from input ISA.
    const ocsd_isa nextISA() const;                     //!< ISA for next instruction

    // information on error conditions
    const bool isNacc() const;                  //!< true if Memory Not Accessible (nacc) error occurred 
    void clearNacc();                           //!< clear the nacc error flag
    const ocsd_vaddr_t getNaccAddr() const;     //!< get the nacc error address.

private:
    bool initFollowerState();       //!< clear all the o/p data and flags, check init valid.

    ocsd_err_t decodeSingleOpCode();      //!< decode single opcode address from current m_inst_info packet

    ocsd_instr_info m_instr_info;

    ocsd_vaddr_t m_st_range_addr;   //!< start of excuted range - inclusive address.
    ocsd_vaddr_t m_en_range_addr;   //!< end of executed range - exclusive address.
    ocsd_vaddr_t m_next_addr;       //!< calcuated next address (could be eo range of branch address, not set for indirect branches)
    bool m_b_next_valid;            //!< true if next address valid, false if need address from trace packets.

    //! memory space rule to use when accessing memory.
    ocsd_mem_space_acc_t m_mem_acc_rule;
    //! memory space csid to use when accessing memory.
    uint8_t              m_mem_space_csid;
    
    ocsd_vaddr_t m_nacc_address;    //!< memory address that was inaccessible - failed read @ start, or during follow operation
    bool m_b_nacc_err;              //!< memory NACC error - required address was unavailable.

    //! pointers to the memory access and i decode interfaces.
    componentAttachPt<ITargetMemAccess> *m_pMemAccess;
    componentAttachPt<IInstrDecode> *m_pIDecode;

};

#endif // ARM_OCSD_CODE_FOLLOWER_H_INCLUDED

//*********** setup API
inline void OcsdCodeFollower::setArchProfile(const ocsd_arch_profile_t profile)
{
    m_instr_info.pe_type = profile;
}

inline void OcsdCodeFollower::setMemSpaceAccess(const ocsd_mem_space_acc_t mem_acc_rule)
{
    m_mem_acc_rule = mem_acc_rule;
}

inline void  OcsdCodeFollower::setMemSpaceCSID(const uint8_t csid)
{
    m_mem_space_csid = csid;
}

inline void OcsdCodeFollower::setISA(const ocsd_isa isa)
{
    m_instr_info.isa = isa;
}

inline void OcsdCodeFollower::setDSBDMBasWP()
{
    m_instr_info.dsb_dmb_waypoints = 1;
}

//**************************************** results API
inline const ocsd_vaddr_t OcsdCodeFollower::getRangeSt() const
{
    return m_st_range_addr;
}

inline const ocsd_vaddr_t OcsdCodeFollower::getRangeEn() const
{
    return m_en_range_addr;
}

inline const bool OcsdCodeFollower::hasRange() const
{
    return m_st_range_addr < m_en_range_addr;
}

inline const bool OcsdCodeFollower::hasNextAddr() const
{
    return m_b_next_valid;
}

inline const ocsd_vaddr_t OcsdCodeFollower::getNextAddr() const
{
    return m_next_addr;
}

// information on last instruction executed in range.
inline const ocsd_instr_type OcsdCodeFollower::getInstrType() const
{
    return m_instr_info.type;
}

inline const ocsd_instr_subtype OcsdCodeFollower::getInstrSubType() const
{
    return m_instr_info.sub_type;
}

inline const bool OcsdCodeFollower::isCondInstr() const
{
    return (bool)(m_instr_info.is_conditional == 1);
}

inline const bool OcsdCodeFollower::isLink() const
{
    return (bool)(m_instr_info.is_link == 1);
}

inline const bool OcsdCodeFollower::ISAChanged() const
{
    return (bool)(m_instr_info.isa != m_instr_info.next_isa);
}

inline const ocsd_isa OcsdCodeFollower::nextISA() const
{
    return m_instr_info.next_isa;
}

// information on error conditions
inline const bool OcsdCodeFollower::isNacc() const
{
    return m_b_nacc_err;
}

inline void OcsdCodeFollower::clearNacc()
{
    m_b_nacc_err = false;
}

inline const ocsd_vaddr_t OcsdCodeFollower::getNaccAddr() const
{
    return m_nacc_address;
}

/* End of File ocsd_code_follower.h */
