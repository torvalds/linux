/*
 * \file       trc_pkt_elem_etmv4i.h
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

#ifndef ARM_TRC_PKT_ELEM_ETMV4I_H_INCLUDED
#define ARM_TRC_PKT_ELEM_ETMV4I_H_INCLUDED

#include "trc_pkt_types_etmv4.h"
#include "common/trc_printable_elem.h"
#include "common/trc_pkt_elem_base.h"

/** @addtogroup trc_pkts
@{*/

/*!
* @class Etmv4PktAddrStack
* @brief ETMv4 Address packet values stack
* @ingroup trc_pkts
*
*  This class represents a stack of recent broadcast address values - 
*  used to fulfil the ExactMatch address type where no address is output.
*
*/
class Etmv4PktAddrStack
{
public:
    Etmv4PktAddrStack()
    {
        for (int i = 0; i < 3; i++)
        {
            m_v_addr[i].pkt_bits = 0;
            m_v_addr[i].size = VA_64BIT;
            m_v_addr[i].val = 0;
            m_v_addr[i].valid_bits = 0;
            m_v_addr_ISA[i] = 0;
        }
    }
    ~Etmv4PktAddrStack() {};

    void push(const ocsd_pkt_vaddr vaddr, const uint8_t isa)
    {
        m_v_addr[2] = m_v_addr[1];
        m_v_addr[1] = m_v_addr[0];
        m_v_addr[0] = vaddr;
        m_v_addr_ISA[2] = m_v_addr_ISA[1];
        m_v_addr_ISA[1] = m_v_addr_ISA[0];
        m_v_addr_ISA[0] = isa;
    }

    void get_idx(const uint8_t idx, ocsd_pkt_vaddr &vaddr, uint8_t &isa)
    {
        if (idx < 3)
        {
            vaddr = m_v_addr[idx];
            isa = m_v_addr_ISA[idx];
        }
    }

private:
    ocsd_pkt_vaddr m_v_addr[3];         //!< most recently broadcast address packet
    uint8_t        m_v_addr_ISA[3];
};

/*!
 * @class EtmV4ITrcPacket   
 * @brief ETMv4 Instuction Trace Protocol Packet.
 * @ingroup trc_pkts
 * 
 *  This class represents a single ETMv4 data trace packet, along with intra packet state.
 * 
 */
class EtmV4ITrcPacket :  public TrcPacketBase, public ocsd_etmv4_i_pkt, public trcPrintableElem
{
public:
    EtmV4ITrcPacket();
    ~EtmV4ITrcPacket();

    EtmV4ITrcPacket &operator =(const ocsd_etmv4_i_pkt* p_pkt);

    virtual const void *c_pkt() const { return (const ocsd_etmv4_i_pkt *)this; };

    // update interface - set packet values
    void initStartState();   //!< Set to initial state - no intra packet state valid. Use on start of trace / discontinuities.
    void initNextPacket();  //!< clear any single packet only flags / state.

    void setType(const ocsd_etmv4_i_pkt_type pkt_type) { type = pkt_type; };
    void updateErrType(const ocsd_etmv4_i_pkt_type err_pkt_type);

    void clearTraceInfo();  //!< clear all the trace info data prior to setting for new trace info packet.
    void setTraceInfo(const uint32_t infoVal);
    void setTraceInfoKey(const uint32_t keyVal);
    void setTraceInfoSpec(const uint32_t specVal);
    void setTraceInfoCyct(const uint32_t cyctVal);

    void setTS(const uint64_t value, const uint8_t bits);
    void setCycleCount(const uint32_t value);
    void setCommitElements(const uint32_t commit_elem);
    void setCancelElements(const uint32_t cancel_elem);
    void setAtomPacket(const ocsd_pkt_atm_type type, const uint32_t En_bits, const uint8_t num);

    void setCondIF1(uint32_t const cond_key);
    void setCondIF2(uint8_t const c_elem_idx);
    void setCondIF3(uint8_t const num_c_elem, const bool finalElem);

    void setCondRF1(const uint32_t key[2], const uint8_t res[2], const uint8_t CI[2], const bool set2Keys);
    void setCondRF2(const uint8_t key_incr, const uint8_t token);
    void setCondRF3(const uint16_t tokens);
    void setCondRF4(const uint8_t token);

    void setContextInfo(const bool update, const uint8_t EL = 0, const uint8_t NS = 0, const uint8_t SF = 0);
    void setContextVMID(const uint32_t VMID);
    void setContextCID(const uint32_t CID);

    void setExceptionInfo(const uint16_t excep_type, const uint8_t addr_interp, const uint8_t m_fault_pending, const uint8_t m_type);
    
    void set64BitAddress(const uint64_t addr, const uint8_t IS);
    void set32BitAddress(const uint32_t addr, const uint8_t IS);
    void updateShortAddress(const uint32_t addr, const uint8_t IS, const uint8_t update_bits);
    void setAddressExactMatch(const uint8_t idx);

    void setDataSyncMarker(const uint8_t dsm_val);
    void setEvent(const uint8_t event_val);

    void setQType(const bool has_count, const uint32_t count, const bool has_addr, const bool addr_match, const uint8_t type);

    // packet status interface - get packet info.
    const ocsd_etmv4_i_pkt_type getType() const { return type; };
    const ocsd_etmv4_i_pkt_type getErrType() const { return err_type; };

    //! return true if this packet has set the commit packet count.
    const bool hasCommitElementsCount() const 
    {
        return pkt_valid.bits.commit_elem_valid ? true : false;
    };
    
    // trace info
    const etmv4_trace_info_t &getTraceInfo() const { return trace_info; };    
    const uint32_t getCCThreshold() const;
    const uint32_t getP0Key() const;
    const uint32_t getCurrSpecDepth() const;

    // atom
    const ocsd_pkt_atom &getAtom() const { return atom; };

    // context
    const etmv4_context_t &getContext() const { return context; };

    // address
    const uint8_t &getAddrMatch() const  { return addr_exact_match_idx; };
    const ocsd_vaddr_t &getAddrVal() const { return v_addr.val; };
    const uint8_t &getAddrIS() const { return v_addr_ISA; };
    const bool getAddr64Bit() const { return v_addr.size == VA_64BIT; };

    // ts
    const uint64_t getTS() const { return pkt_valid.bits.ts_valid ? ts.timestamp : 0; };

    // cc
    const uint32_t getCC() const { return pkt_valid.bits.cc_valid ? cycle_count : 0; };

    // packet type
    const bool isBadPacket() const;

    // printing
    virtual void toString(std::string &str) const;
    virtual void toStringFmt(const uint32_t fmtFlags, std::string &str) const;

private:
    const char *packetTypeName(const ocsd_etmv4_i_pkt_type type, const char **pDesc) const;
    void contextStr(std::string &ctxtStr) const;
    void atomSeq(std::string &valStr) const;
    void addrMatchIdx(std::string &valStr) const;
    void exceptionInfo(std::string &valStr) const;

    void push_vaddr();
    void pop_vaddr_idx(const uint8_t idx);

    Etmv4PktAddrStack m_addr_stack;
};

inline void  EtmV4ITrcPacket::updateErrType(const ocsd_etmv4_i_pkt_type err_pkt_type)
{
    // set primary type to incoming error type, set packet err type to previous primary type.
    err_type = type;
    type = err_pkt_type;
}

inline void EtmV4ITrcPacket::clearTraceInfo()
{
    pkt_valid.bits.ts_valid = 0;
    pkt_valid.bits.trace_info_valid = 0;
    pkt_valid.bits.p0_key_valid = 0;
    pkt_valid.bits.spec_depth_valid = 0;
    pkt_valid.bits.cc_thresh_valid  = 0;

    pkt_valid.bits.ts_valid = 0;    // mark TS as invalid - must be re-updated after trace info.
}

inline void EtmV4ITrcPacket::setTraceInfo(const uint32_t infoVal)
{
    trace_info.val = infoVal;
    pkt_valid.bits.trace_info_valid = 1;
}

inline void EtmV4ITrcPacket::setTraceInfoKey(const uint32_t keyVal)
{
    p0_key = keyVal;
    pkt_valid.bits.p0_key_valid = 1;
}

inline void EtmV4ITrcPacket::setTraceInfoSpec(const uint32_t specVal)
{
    curr_spec_depth = specVal;
    pkt_valid.bits.spec_depth_valid = 1;
}

inline void EtmV4ITrcPacket::setTraceInfoCyct(const uint32_t cyctVal)
{
    cc_threshold = cyctVal;
    pkt_valid.bits.cc_thresh_valid  = 1;
}

inline void EtmV4ITrcPacket::setTS(const uint64_t value, const uint8_t bits)
{
    uint64_t mask = (uint64_t)-1LL;
    if(bits < 64) mask = (1ULL << bits) - 1;
    ts.timestamp = (ts.timestamp & ~mask) | (value & mask);
    ts.bits_changed = bits;
    pkt_valid.bits.ts_valid = 1;
}

inline void EtmV4ITrcPacket::setCycleCount(const uint32_t value)
{
    pkt_valid.bits.cc_valid = 1;
    cycle_count = value;
}

inline void EtmV4ITrcPacket::setCommitElements(const uint32_t commit_elem)
{
    pkt_valid.bits.commit_elem_valid = 1;
    commit_elements = commit_elem;
}

inline const uint32_t EtmV4ITrcPacket::getCCThreshold() const
{
    if(pkt_valid.bits.cc_thresh_valid)
        return cc_threshold;
    return 0;
}

inline const uint32_t EtmV4ITrcPacket::getP0Key() const
{
    if(pkt_valid.bits.p0_key_valid)
        return p0_key;
    return 0;
}

inline const uint32_t EtmV4ITrcPacket::getCurrSpecDepth() const
{
    if(pkt_valid.bits.spec_depth_valid)
        return curr_spec_depth;
    return 0;
}

inline void EtmV4ITrcPacket::setCancelElements(const uint32_t cancel_elem)
{
    cancel_elements = cancel_elem;
}

inline void EtmV4ITrcPacket::setAtomPacket(const ocsd_pkt_atm_type type, const uint32_t En_bits, const uint8_t num)
{
    if(type == ATOM_REPEAT)
    {
        uint32_t bit_patt = En_bits & 0x1;
        if(bit_patt)
        {   
            // none zero - all 1s
            bit_patt = (bit_patt << num) - 1;
        }
        atom.En_bits = bit_patt;
    }
    else
        atom.En_bits = En_bits;
    atom.num = num;    
}

inline void EtmV4ITrcPacket::setCondIF1(const uint32_t cond_key)
{
    cond_instr.cond_key_set = 1;
    cond_instr.f3_final_elem = 0;
    cond_instr.f2_cond_incr = 0;
    cond_instr.num_c_elem = 1;
    cond_instr.cond_c_key = cond_key;
}

inline void EtmV4ITrcPacket::setCondIF2(const uint8_t c_elem_idx)
{
    cond_instr.cond_key_set = 0;
    cond_instr.f3_final_elem = 0;
    switch(c_elem_idx & 0x3)
    {
    case 0:
        cond_instr.f2_cond_incr = 1;
        cond_instr.num_c_elem = 1;
        break;

    case 1:
        cond_instr.f2_cond_incr = 0;
        cond_instr.num_c_elem = 1;
        break;

    case 2:
        cond_instr.f2_cond_incr = 1;
        cond_instr.num_c_elem = 2;
        break;
    }
}

inline void EtmV4ITrcPacket::setCondIF3(const uint8_t num_c_elem, const bool finalElem)
{
    cond_instr.cond_key_set = 0;
    cond_instr.f3_final_elem = finalElem ? 1: 0;
    cond_instr.f2_cond_incr = 0;
    cond_instr.num_c_elem = num_c_elem;
}

inline void EtmV4ITrcPacket::setCondRF1(const uint32_t key[2], const uint8_t res[2], const uint8_t CI[2],const bool set2Keys)
{
    cond_result.key_res_0_set = 1;
    cond_result.cond_r_key_0 = key[0];
    cond_result.res_0 = res[0];
    cond_result.ci_0 = CI[0];

    if(set2Keys)
    {
        cond_result.key_res_1_set = 1;
        cond_result.cond_r_key_1 = key[1];
        cond_result.res_1 = res[1];
        cond_result.ci_1 = CI[1];
    }
}


inline void EtmV4ITrcPacket::setCondRF2(const uint8_t key_incr, const uint8_t token)
{
    cond_result.key_res_0_set = 0;
    cond_result.key_res_1_set = 0;
    cond_result.f2_key_incr = key_incr;
    cond_result.f2f4_token = token;
}

inline void EtmV4ITrcPacket::setCondRF3(const uint16_t tokens)
{
    cond_result.key_res_0_set = 0;
    cond_result.key_res_1_set = 0;
    cond_result.f3_tokens = tokens;
}

inline void EtmV4ITrcPacket::setCondRF4(const uint8_t token)
{
    cond_result.key_res_0_set = 0;
    cond_result.key_res_1_set = 0;
    cond_result.f2f4_token = token;
}

inline void EtmV4ITrcPacket::setContextInfo(const bool update, const uint8_t EL, const uint8_t NS, const uint8_t SF)
{
    pkt_valid.bits.context_valid = 1;
    if(update)
    {
        context.updated = 1;
        context.EL = EL;
        context.NS = NS;
        context.SF = SF;
    }
}

inline void EtmV4ITrcPacket::setContextVMID(const uint32_t VMID)
{
    pkt_valid.bits.context_valid = 1;
    context.updated = 1;
    context.VMID = VMID;
    context.updated_v = 1;
}

inline void EtmV4ITrcPacket::setContextCID(const uint32_t CID)
{
    pkt_valid.bits.context_valid = 1;
    context.updated = 1;
    context.ctxtID = CID;
    context.updated_c = 1;
}

inline void EtmV4ITrcPacket::setExceptionInfo(const uint16_t excep_type, const uint8_t addr_interp, const uint8_t m_fault_pending, const uint8_t m_type)
{
    exception_info.exceptionType = excep_type;
    exception_info.addr_interp = addr_interp;
    exception_info.m_fault_pending = m_fault_pending;
    exception_info.m_type = m_type;
}

inline void EtmV4ITrcPacket::set64BitAddress(const uint64_t addr, const uint8_t IS)
{
    v_addr.pkt_bits = 64;
    v_addr.valid_bits = 64;
    v_addr.size = VA_64BIT;
    v_addr.val = addr;
    v_addr_ISA = IS;
    push_vaddr();
}

inline void EtmV4ITrcPacket::set32BitAddress(const uint32_t addr, const uint8_t IS)
{
    uint64_t mask = OCSD_BIT_MASK(32);
    v_addr.pkt_bits = 32;

    if (pkt_valid.bits.context_valid && context.SF)
        v_addr.size = VA_64BIT;
    else
    {
        v_addr.val &= 0xFFFFFFFF;   // ensure vaddr is only 32 bits if not 64 bit 
        v_addr.size = VA_32BIT;
    }

    if (v_addr.valid_bits < 32) // may be 64 bit address so only set 32 if less
        v_addr.valid_bits = 32;

    v_addr.val = (v_addr.val & ~mask) | (addr & mask);
    v_addr_ISA = IS;
    push_vaddr();
}

inline void EtmV4ITrcPacket::updateShortAddress(const uint32_t addr, const uint8_t IS, const uint8_t update_bits)
{
    ocsd_vaddr_t update_mask = OCSD_BIT_MASK(update_bits);
    v_addr.pkt_bits = update_bits;
    if(v_addr.valid_bits < update_bits)
        v_addr.valid_bits = update_bits;

    v_addr.val = (v_addr.val & ~update_mask) | (addr & update_mask);
    v_addr_ISA = IS;
    push_vaddr();
}

inline void EtmV4ITrcPacket::setAddressExactMatch(const uint8_t idx)
{
    addr_exact_match_idx = idx;   
    pop_vaddr_idx(idx);
    push_vaddr();
}

inline void EtmV4ITrcPacket::setDataSyncMarker(const uint8_t dsm_value)
{
    dsm_val = dsm_value;
}

inline void EtmV4ITrcPacket::setEvent(const uint8_t event_value)
{
    event_val = event_value;
}

inline void EtmV4ITrcPacket::setQType(const bool has_count, const uint32_t count, const bool has_addr, const bool addr_match, const uint8_t type)
{
    Q_pkt.q_count = count;
    Q_pkt.q_type = type;
    Q_pkt.count_present = has_count ? 1 : 0;
    Q_pkt.addr_present = has_addr ? 1: 0;
    Q_pkt.addr_match = addr_match ? 1 :0;
}

inline const bool EtmV4ITrcPacket::isBadPacket() const
{
    return (type >= ETM4_PKT_I_BAD_SEQUENCE);
}

inline void  EtmV4ITrcPacket::push_vaddr()
{
    m_addr_stack.push(v_addr, v_addr_ISA);
}

inline void EtmV4ITrcPacket::pop_vaddr_idx(const uint8_t idx)
{
    m_addr_stack.get_idx(idx, v_addr, v_addr_ISA);
}

/** @}*/

#endif // ARM_TRC_PKT_ELEM_ETMV4I_H_INCLUDED

/* End of File trc_pkt_elem_etmv4i.h */
