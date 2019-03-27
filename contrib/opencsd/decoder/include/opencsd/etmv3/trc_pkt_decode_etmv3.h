/*!
 * \file       trc_pkt_decode_etmv3.h
 * \brief      OpenCSD : ETMv3 decode
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

#ifndef ARM_TRC_PKT_DECODE_ETMV3_H_INCLUDED
#define ARM_TRC_PKT_DECODE_ETMV3_H_INCLUDED

#include "common/trc_pkt_decode_base.h"
#include "common/trc_gen_elem.h"
#include "common/ocsd_pe_context.h"
#include "common/ocsd_code_follower.h"
#include "common/ocsd_gen_elem_list.h"

#include "opencsd/etmv3/trc_pkt_elem_etmv3.h"
#include "opencsd/etmv3/trc_cmp_cfg_etmv3.h"

/**************** Atom handling class **************************************/
class Etmv3Atoms
{
public:
    Etmv3Atoms(const bool isCycleAcc);
    ~Etmv3Atoms() {};

    //! initialise the atom and index values
    void initAtomPkt(const EtmV3TrcPacket *in_pkt, const ocsd_trc_index_t &root_index);
    
    const ocsd_atm_val getCurrAtomVal() const;
    const int numAtoms() const; //!< number of atoms
    const ocsd_trc_index_t pktIndex() const; //!< originating packet index

    const bool hasAtomCC() const;   //!< cycle count for current atom?
    const uint32_t getAtomCC() const;   //!< cycle count for current atom
    const uint32_t getRemainCC() const; //!< get residual cycle count for remaining atoms

    void clearAtom();   //!< clear the current atom, set the next.
    void clearAll();    //!< clear all    

private:

    // Atom PHDR packet formats from ETMv3 spec - defines content of header.
    enum {
        ATOM_PHDR_FMT_1 = 1,
        ATOM_PHDR_FMT_2,
        ATOM_PHDR_FMT_3,
        ATOM_PHDR_FMT_4,
    };



    ocsd_pkt_atom m_atom;         /**< atom elements - non zero number indicates valid atom count */
    uint8_t m_p_hdr_fmt;          /**< if atom elements, associated phdr format */
    uint32_t m_cycle_count;       
    ocsd_trc_index_t m_root_index; //!< root index for the atom packet
    bool m_isCCPacket;
};


inline Etmv3Atoms::Etmv3Atoms(const bool isCycleAcc)
{
    m_isCCPacket = isCycleAcc;
}

//! initialise the atom and index values
inline void Etmv3Atoms::initAtomPkt(const EtmV3TrcPacket *in_pkt, const ocsd_trc_index_t &root_index)
{
    m_atom = in_pkt->getAtom();
    m_p_hdr_fmt = in_pkt->getPHdrFmt();
    m_cycle_count = in_pkt->getCycleCount();
}
    
inline const ocsd_atm_val Etmv3Atoms::getCurrAtomVal() const
{
    return (m_atom.En_bits & 0x1) ? ATOM_E : ATOM_N;
}

inline const int Etmv3Atoms::numAtoms() const
{
    return m_atom.num;
}

inline const ocsd_trc_index_t Etmv3Atoms::pktIndex() const
{
    return  m_root_index;
}

inline const bool Etmv3Atoms::hasAtomCC() const
{
    bool hasCC = false;
    if(!m_isCCPacket)
        return hasCC;

    switch(m_p_hdr_fmt)
    {
    case ATOM_PHDR_FMT_4:
    default: 
        break;

    case ATOM_PHDR_FMT_3:
    case ATOM_PHDR_FMT_1: 
        hasCC = true;
        break;

    case ATOM_PHDR_FMT_2:
        hasCC = (m_atom.num > 1);   // first of 2 has W state
        break;
    }
    return hasCC;
}

inline const uint32_t Etmv3Atoms::getAtomCC() const
{
    uint32_t CC = 0;
    if(!m_isCCPacket)
        return CC;

    switch(m_p_hdr_fmt)
    {
    case ATOM_PHDR_FMT_4: // no CC in format 4
    default:  
        break;

    case ATOM_PHDR_FMT_3: // single CC with optional E atom
        CC = m_cycle_count; 
        break;

    case ATOM_PHDR_FMT_2: // single W on first of 2 atoms
        CC = (m_atom.num > 1) ? 1: 0; 
        break;

    case ATOM_PHDR_FMT_1: // each atom has 1 CC.
        CC = 1; 
        break;
    }
    return CC;
}

inline const uint32_t Etmv3Atoms::getRemainCC() const
{
    uint32_t CC = 0;
    if(!m_isCCPacket)
        return CC;

    switch(m_p_hdr_fmt)
    {
    case ATOM_PHDR_FMT_4: // no CC in format 4
    default:  
        break;

    case ATOM_PHDR_FMT_3:
        CC = m_cycle_count; 
        break;

    case ATOM_PHDR_FMT_2:
        CC = (m_atom.num > 1) ? 1: 0; 
        break;

    case ATOM_PHDR_FMT_1:
        CC = m_atom.num; 
        break;
    }
    return CC;
}

inline void Etmv3Atoms::clearAtom()
{
    m_atom.En_bits >>=1;
    if(m_atom.num)
        m_atom.num--;
}

inline void Etmv3Atoms::clearAll()
{
    m_atom.num = 0;
}

/********** Main decode class ****************************************************/
class TrcPktDecodeEtmV3 :  public TrcPktDecodeBase<EtmV3TrcPacket, EtmV3Config>
{
public:
    TrcPktDecodeEtmV3();
    TrcPktDecodeEtmV3(int instIDNum);
    virtual ~TrcPktDecodeEtmV3();

protected:
    /* implementation packet decoding interface */
    virtual ocsd_datapath_resp_t processPacket();
    virtual ocsd_datapath_resp_t onEOT();
    virtual ocsd_datapath_resp_t onReset();
    virtual ocsd_datapath_resp_t onFlush();
    virtual ocsd_err_t onProtocolConfig();
    virtual const uint8_t getCoreSightTraceID() { return m_CSID; };

    /* local decode methods */
    void initDecoder();      //!< initial state on creation (zeros all config)
    void resetDecoder();     //!< reset state to start of decode. (moves state, retains config)

    ocsd_datapath_resp_t decodePacket(bool &pktDone); //!< decode a packet 

    ocsd_datapath_resp_t processISync(const bool withCC, const bool firstSync = false);
    ocsd_datapath_resp_t processBranchAddr();
    ocsd_datapath_resp_t processPHdr();
    
    ocsd_datapath_resp_t sendUnsyncPacket();    //!< send an initial unsync packet when decoder starts

    OcsdTraceElement *GetNextOpElem(ocsd_datapath_resp_t &resp);    //!< get the next element from the element list.

private:
    void setNeedAddr(bool bNeedAddr);
    void pendExceptionReturn();
    bool preISyncValid(ocsd_etmv3_pkt_type pkt_type);
//** intra packet state;

    OcsdCodeFollower m_code_follower;   //!< code follower for instruction trace

    ocsd_vaddr_t m_IAddr;           //!< next instruction address
    bool m_bNeedAddr;               //!< true if an address is needed (current out of date / invalid)
    bool m_bSentUnknown;            //!< true if we have sent an unknown address packet for this phase of needing an address.
    bool m_bWaitISync;              //!< true if waiting for first ISync packet

    OcsdPeContext m_PeContext;      //!< save context data before sending in output packet

    OcsdGenElemList m_outputElemList;   //!< list of output elements


//** Other packet decoder state;

    // trace decode FSM
    typedef enum {
        NO_SYNC,        //!< pre start trace - init state or after reset or overflow, loss of sync.
        WAIT_ASYNC,     //!< waiting for a-sync packet.
        WAIT_ISYNC,     //!< waiting for i-sync packet.
        DECODE_PKTS,    //!< processing a packet
        SEND_PKTS,      //!< sending packets.
    } processor_state_t;

    processor_state_t m_curr_state;

    uint8_t m_CSID; //!< Coresight trace ID for this decoder.
};


#endif // ARM_TRC_PKT_DECODE_ETMV3_H_INCLUDED

/* End of File trc_pkt_decode_etmv3.h */
