/*
 * \file       trc_pkt_elem_etmv3.h
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

#ifndef ARM_TRC_PKT_ELEM_ETMV3_H_INCLUDED
#define ARM_TRC_PKT_ELEM_ETMV3_H_INCLUDED

#include "trc_pkt_types_etmv3.h"
#include "common/trc_printable_elem.h"
#include "common/trc_pkt_elem_base.h"

/** @addtogroup trc_pkts
@{*/

/*!
 * @class EtmV3TrcPacket   
 * @brief ETMv3 Trace Protocol Packet.
 * 
 *  This class represents a single ETMv3 trace packet, along with intra packet state.
 * 
 */
class EtmV3TrcPacket : public TrcPacketBase, public trcPrintableElem
{
public:
    EtmV3TrcPacket();
    ~EtmV3TrcPacket();

// conversions between C-API struct and C++ object types
    // assign from C-API struct
    EtmV3TrcPacket &operator =(const ocsd_etmv3_pkt* p_pkt);

    // allow const cast to C-API struct to pass C++ object
    operator const ocsd_etmv3_pkt*() const { return &m_pkt_data; };
    operator const ocsd_etmv3_pkt&() const { return m_pkt_data; };

    // override c_pkt to pass out the packet data struct.
    virtual const void *c_pkt() const { return &m_pkt_data; };

// update interface - set packet values
    void Clear();       //!< clear update data in packet ready for new one.
    void ResetState();  //!< reset intra packet state data -on full decoder reset.

    void SetType(const ocsd_etmv3_pkt_type p_type);
    void SetErrType(const ocsd_etmv3_pkt_type e_type);
    void UpdateAddress(const ocsd_vaddr_t partAddrVal, const int updateBits);
    void SetException(  const ocsd_armv7_exception type, 
                        const uint16_t number, 
                        const bool cancel,
                        const bool cm_type,
                        const int irq_n = 0,
                        const int resume = 0);
    void UpdateNS(const int NS);
    void UpdateAltISA(const int AltISA);
    void UpdateHyp(const int Hyp);
    void UpdateISA(const ocsd_isa isa);
    void UpdateContextID(const uint32_t contextID);
    void UpdateVMID(const uint8_t VMID);
    void UpdateTimestamp(const uint64_t tsVal, const uint8_t updateBits);
    
    bool UpdateAtomFromPHdr(const uint8_t pHdr, const bool cycleAccurate);  //!< Interpret P Hdr, return true if valid, false if not.

    void SetDataOOOTag(const uint8_t tag);
    void SetDataValue(const uint32_t value);
    void UpdateDataAddress(const uint32_t value, const uint8_t valid_bits);
    void UpdateDataEndian(const uint8_t BE_Val);
    void SetCycleCount(const uint32_t cycleCount);
    void SetISyncReason(const ocsd_iSync_reason reason);
    void SetISyncHasCC();
    void SetISyncIsLSiP();
    void SetISyncNoAddr();

// packet status interface - get packet info.
    const ocsd_etmv3_pkt_type getType() const { return m_pkt_data.type; };
    const bool isBadPacket() const;
        
    const int AltISA() const { return m_pkt_data.context.curr_alt_isa; };
    const ocsd_isa ISA() const { return m_pkt_data.curr_isa; };
    const bool changedISA() const { return m_pkt_data.curr_isa != m_pkt_data.prev_isa; };

    // any of the context elements updated?
    const bool isCtxtUpdated() const; 
    const bool isCtxtFlagsUpdated() const { return (m_pkt_data.context.updated == 1); };
    const bool isNS() const { return m_pkt_data.context.curr_NS; };
    const bool isHyp() const { return m_pkt_data.context.curr_Hyp; };

    const bool isCtxtIDUpdated() const { return (m_pkt_data.context.updated_c == 1); }
    const uint32_t getCtxtID() const { return m_pkt_data.context.ctxtID; };
    const bool isVMIDUpdated() const { return (m_pkt_data.context.updated_v == 1); }
    const uint32_t getVMID() const { return m_pkt_data.context.VMID; };

    const uint32_t getCycleCount() const { return m_pkt_data.cycle_count; };
    const uint64_t getTS() const { return m_pkt_data.timestamp; };
    
    const bool isExcepPkt() const { return (m_pkt_data.exception.bits.present == 1); };
    const ocsd_armv7_exception excepType() const { return m_pkt_data.exception.type; };
    const uint16_t excepNum() const { return m_pkt_data.exception.number; };
    const bool isExcepCancel() const { return (m_pkt_data.exception.bits.present == 1) && (m_pkt_data.exception.bits.cancel == 1); };

    const ocsd_iSync_reason getISyncReason() const { return m_pkt_data.isync_info.reason; };
    const bool getISyncHasCC() const { return m_pkt_data.isync_info.has_cycle_count; };
    const bool getISyncIsLSiPAddr() const { return m_pkt_data.isync_info.has_LSipAddress; };
    const bool getISyncNoAddr() const { return m_pkt_data.isync_info.no_address; };

    const ocsd_vaddr_t getAddr() const { return  m_pkt_data.addr.val; };
    const ocsd_vaddr_t getDataAddr() const { return  m_pkt_data.data.addr.val; };

    const ocsd_pkt_atom &getAtom() const { return m_pkt_data.atom; };
    const uint8_t getPHdrFmt() const { return m_pkt_data.p_hdr_fmt; };


// printing
    virtual void toString(std::string &str) const;
    virtual void toStringFmt(const uint32_t fmtFlags, std::string &str) const;
    
private:
    const char *packetTypeName(const ocsd_etmv3_pkt_type type, const char **ppDesc) const;
    void getBranchAddressStr(std::string &valStr) const;
    void getAtomStr(std::string &valStr) const;
    void getISyncStr(std::string &valStr) const;
    void getISAStr(std::string &isaStr) const;
    void getExcepStr(std::string &excepStr) const;

    ocsd_etmv3_pkt m_pkt_data; 
};

inline void EtmV3TrcPacket::UpdateNS(const int NS)
{
    m_pkt_data.context.curr_NS = NS;
    m_pkt_data.context.updated = 1;
};

inline void EtmV3TrcPacket::UpdateAltISA(const int AltISA)
{
    m_pkt_data.context.curr_alt_isa = AltISA;
    m_pkt_data.context.updated = 1;
}

inline void EtmV3TrcPacket::UpdateHyp(const int Hyp)
{
    m_pkt_data.context.curr_Hyp = Hyp;
    m_pkt_data.context.updated = 1;
}

inline void EtmV3TrcPacket::UpdateISA(const ocsd_isa isa)
{
    m_pkt_data.prev_isa = m_pkt_data.curr_isa;
    m_pkt_data.curr_isa = isa;
}

inline void EtmV3TrcPacket::SetType(const ocsd_etmv3_pkt_type p_type)
{
    m_pkt_data.type = p_type;
}

inline void EtmV3TrcPacket::SetErrType(const ocsd_etmv3_pkt_type e_type)
{
    m_pkt_data.err_type = m_pkt_data.type;
    m_pkt_data.type = e_type;
}

inline const bool EtmV3TrcPacket::isBadPacket() const
{
    return (m_pkt_data.type >= ETM3_PKT_BAD_SEQUENCE);
}

inline void EtmV3TrcPacket::SetDataOOOTag(const uint8_t tag)
{
    m_pkt_data.data.ooo_tag = tag;
}

inline void EtmV3TrcPacket::SetDataValue(const uint32_t value)
{
    m_pkt_data.data.value = value;
    m_pkt_data.data.update_dval = 1;
}

inline void EtmV3TrcPacket::UpdateContextID(const uint32_t contextID)
{
    m_pkt_data.context.updated_c = 1;
    m_pkt_data.context.ctxtID = contextID;
}

inline void EtmV3TrcPacket::UpdateVMID(const uint8_t VMID)
{
    m_pkt_data.context.updated_v = 1;
    m_pkt_data.context.VMID = VMID;
}

inline void EtmV3TrcPacket::UpdateDataEndian(const uint8_t BE_Val)
{
    m_pkt_data.data.be = BE_Val;
    m_pkt_data.data.update_be = 1;
}

inline void EtmV3TrcPacket::SetCycleCount(const uint32_t cycleCount)
{
    m_pkt_data.cycle_count = cycleCount;
}

inline void EtmV3TrcPacket::SetISyncReason(const ocsd_iSync_reason reason)
{
    m_pkt_data.isync_info.reason = reason;
}

inline void EtmV3TrcPacket::SetISyncHasCC()
{
    m_pkt_data.isync_info.has_cycle_count = 1;
}

inline void EtmV3TrcPacket::SetISyncIsLSiP()
{
    m_pkt_data.isync_info.has_LSipAddress = 1;
}

inline void EtmV3TrcPacket::SetISyncNoAddr()
{
    m_pkt_data.isync_info.no_address = 1;
}

inline const bool EtmV3TrcPacket::isCtxtUpdated() const
{
     return (m_pkt_data.context.updated_v == 1) ||
            (m_pkt_data.context.updated == 1) ||
            (m_pkt_data.context.updated_c == 1);
}

/** @}*/
#endif // ARM_TRC_PKT_ELEM_ETMV3_H_INCLUDED

/* End of File trc_pkt_elem_etmv3.h */
