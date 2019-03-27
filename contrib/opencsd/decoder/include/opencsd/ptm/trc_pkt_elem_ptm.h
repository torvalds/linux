/*
 * \file       trc_pkt_elem_ptm.h
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


#ifndef ARM_TRC_PKT_ELEM_PTM_H_INCLUDED
#define ARM_TRC_PKT_ELEM_PTM_H_INCLUDED

#include "trc_pkt_types_ptm.h"
#include "common/trc_printable_elem.h"
#include "common/trc_pkt_elem_base.h"

/** @addtogroup trc_pkts
@{*/


class PtmTrcPacket :  public TrcPacketBase, public ocsd_ptm_pkt, trcPrintableElem
{
public:
    PtmTrcPacket();
    ~PtmTrcPacket();

    PtmTrcPacket &operator =(const ocsd_ptm_pkt* p_pkt);

    virtual const void *c_pkt() const { return (const ocsd_ptm_pkt *)this; };

    // update interface - set packet values

    void Clear();       //!< clear update data in packet ready for new one.
    void ResetState();  //!< reset intra packet state data - on full decoder reset.

    void SetType(const ocsd_ptm_pkt_type p_type);
    void SetErrType(const ocsd_ptm_pkt_type e_type);

    void SetException(  const ocsd_armv7_exception type, 
                        const uint16_t number);
    void SetISyncReason(const ocsd_iSync_reason reason);
    void SetCycleCount(const uint32_t cycleCount);
    void SetAtomFromPHdr(const uint8_t pHdr);
    void SetCycleAccAtomFromPHdr(const uint8_t pHdr);
    
    void UpdateAddress(const ocsd_vaddr_t partAddrVal, const int updateBits);
    void UpdateNS(const int NS);
    void UpdateAltISA(const int AltISA);
    void UpdateHyp(const int Hyp);
    void UpdateISA(const ocsd_isa isa);
    void UpdateContextID(const uint32_t contextID);
    void UpdateVMID(const uint8_t VMID);
    void UpdateTimestamp(const uint64_t tsVal, const uint8_t updateBits);

    // packet status interface 
    
    //  get packet info.
    const bool isBadPacket() const;
    const ocsd_ptm_pkt_type getType() const;

    // isa
    const ocsd_isa getISA() const;
    const bool ISAChanged() const { return (bool)(curr_isa != prev_isa); };
    const uint8_t getAltISA() const { return context.curr_alt_isa; };
    const uint8_t getNS() const { return context.curr_NS; };
    const uint8_t getHyp() const { return context.curr_Hyp; };

    // address 
    const ocsd_vaddr_t getAddrVal() const { return addr.val; }; 

    // pe context information
    const bool CtxtIDUpdated() const { return (bool)(context.updated_c == 1); };
    const bool VMIDUpdated() const { return (bool)(context.updated_v == 1); };
    const uint32_t getCtxtID() const { return context.ctxtID; };
    const uint8_t getVMID() const { return context.VMID; };
    const bool PEContextUpdated() const { return context.updated; };

    // atom info
    const ocsd_pkt_atom &getAtom() const { return atom; };

    // branch address info
    const bool isBranchExcepPacket() const { return (exception.bits.present == 1); };
    const ocsd_armv7_exception excepType() const { return exception.type; };
    const uint16_t excepNum() const { return exception.number;  };

    // isync 
    const ocsd_iSync_reason iSyncReason() const { return i_sync_reason; };

    // cycle count 
    const bool hasCC() const { return (cc_valid == 1); };
    const uint32_t getCCVal() const { return cycle_count; };

    // printing
    virtual void toString(std::string &str) const;
    virtual void toStringFmt(const  uint32_t fmtFlags, std::string &str) const;

private:
    void packetTypeName(const ocsd_ptm_pkt_type pkt_type, std::string &name, std::string &desc) const;
    void getAtomStr(std::string &valStr) const;
    void getBranchAddressStr(std::string &valStr) const;
    void getExcepStr(std::string &excepStr) const;
    void getISAStr(std::string &isaStr) const;
    void getCycleCountStr(std::string &subStr) const;
    void getISyncStr(std::string &valStr) const;
    void getTSStr(std::string &valStr) const;
};


//*** update interface - set packet values
inline void PtmTrcPacket::SetType(const ocsd_ptm_pkt_type p_type)
{
    type = p_type;
}

inline void PtmTrcPacket::SetErrType(const ocsd_ptm_pkt_type e_type)
{
    err_type = type;
    type = e_type;
}

inline void PtmTrcPacket::UpdateNS(const int NS)
{
    context.curr_NS = NS;
    context.updated = 1;
};

inline void PtmTrcPacket::UpdateAltISA(const int AltISA)
{
    context.curr_alt_isa = AltISA;
    context.updated = 1;
}

inline void PtmTrcPacket::UpdateHyp(const int Hyp)
{
    context.curr_Hyp = Hyp;
    context.updated = 1;
}

inline void PtmTrcPacket::UpdateISA(const ocsd_isa isa)
{
    prev_isa = curr_isa;
    curr_isa = isa;
}

inline void PtmTrcPacket::UpdateContextID(const uint32_t contextID)
{
    context.ctxtID = contextID;
    context.updated_c = 1;
}

inline void PtmTrcPacket::UpdateVMID(const uint8_t VMID)
{
    context.VMID = VMID;
    context.updated_v = 1;
}

inline void PtmTrcPacket::SetException(  const ocsd_armv7_exception type, const uint16_t number)
{
    exception.bits.present = 1;
    exception.number = number;
    exception.type = type;
}

inline void PtmTrcPacket::SetISyncReason(const ocsd_iSync_reason reason)
{
    i_sync_reason = reason;
}

inline void PtmTrcPacket::SetCycleCount(const uint32_t cycleCount)
{
    cycle_count = cycleCount;
    cc_valid = 1;
}

//*** packet status interface - get packet info.
inline const bool PtmTrcPacket::isBadPacket() const
{
    return (bool)(type >= PTM_PKT_BAD_SEQUENCE);
}

inline const ocsd_ptm_pkt_type PtmTrcPacket::getType() const
{
    return type;
}

inline const ocsd_isa PtmTrcPacket::getISA() const
{
    return curr_isa;
}

/** @}*/
#endif // ARM_TRC_PKT_ELEM_PTM_H_INCLUDED

/* End of File trc_pkt_elem_ptm.h */
