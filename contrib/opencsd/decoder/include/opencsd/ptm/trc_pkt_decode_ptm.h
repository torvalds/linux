/*
 * \file       trc_pkt_decode_ptm.h
 * \brief      OpenCSD : PTM packet decoder.
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
#ifndef ARM_TRC_PKT_DECODE_PTM_H_INCLUDED
#define ARM_TRC_PKT_DECODE_PTM_H_INCLUDED

#include "common/trc_pkt_decode_base.h"
#include "opencsd/ptm/trc_pkt_elem_ptm.h"
#include "opencsd/ptm/trc_cmp_cfg_ptm.h"
#include "common/trc_gen_elem.h"
#include "common/trc_ret_stack.h"

/**************** Atom handling class **************************************/
class PtmAtoms
{
public:
    PtmAtoms() {};
    ~PtmAtoms() {};

    //! initialise the atom and index values
    void initAtomPkt(const ocsd_pkt_atom &atom, const ocsd_trc_index_t &root_index);
    
    const ocsd_atm_val getCurrAtomVal() const;
    const int numAtoms() const; //!< number of atoms
    const ocsd_trc_index_t pktIndex() const; //!< originating packet index

    void clearAtom();   //!<  clear the current atom, set the next.
    void clearAll(); //!< clear all

private:
    ocsd_pkt_atom m_atom;
    ocsd_trc_index_t m_root_index; //!< root index for the atom packet
};

inline void PtmAtoms::initAtomPkt(const ocsd_pkt_atom &atom, const ocsd_trc_index_t &root_index)
{
    m_atom = atom;
    m_root_index = root_index;
}
    
inline const ocsd_atm_val PtmAtoms::getCurrAtomVal() const
{
    return (m_atom.En_bits & 0x1) ?  ATOM_E : ATOM_N;
}

inline const int PtmAtoms::numAtoms() const
{
    return m_atom.num;
}

inline const ocsd_trc_index_t PtmAtoms::pktIndex() const
{
    return m_root_index;
}

inline void PtmAtoms::clearAtom()
{
    if(m_atom.num)
    {
        m_atom.num--;
        m_atom.En_bits >>=1;
    }
}

inline void PtmAtoms::clearAll()
{
    m_atom.num = 0;
}

/********** Main decode class ****************************************************/
class TrcPktDecodePtm : public TrcPktDecodeBase<PtmTrcPacket, PtmConfig>
{
public:
    TrcPktDecodePtm();
    TrcPktDecodePtm(int instIDNum);
    virtual ~TrcPktDecodePtm();

protected:
    /* implementation packet decoding interface */
    virtual ocsd_datapath_resp_t processPacket();
    virtual ocsd_datapath_resp_t onEOT();
    virtual ocsd_datapath_resp_t onReset();
    virtual ocsd_datapath_resp_t onFlush();
    virtual ocsd_err_t onProtocolConfig();
    virtual const uint8_t getCoreSightTraceID() { return m_CSID; };

    /* local decode methods */

private:
    /** operation for the trace instruction follower */
    typedef enum {
        TRACE_WAYPOINT,     //!< standard operation - trace to waypoint - default op 
        TRACE_TO_ADDR_EXCL, //!< trace to supplied address - address is 1st instuction not executed.
        TRACE_TO_ADDR_INCL  //!< trace to supplied address - address is last instruction executed.
    } waypoint_trace_t;

    void initDecoder();
    void resetDecoder();

    ocsd_datapath_resp_t decodePacket();
    ocsd_datapath_resp_t contProcess(); 
    ocsd_datapath_resp_t processIsync();
    ocsd_datapath_resp_t processBranch();
    ocsd_datapath_resp_t processWPUpdate();
    ocsd_datapath_resp_t processAtom();
    ocsd_err_t traceInstrToWP(bool &bWPFound, const waypoint_trace_t traceWPOp = TRACE_WAYPOINT, const ocsd_vaddr_t nextAddrMatch = 0);      //!< follow instructions from the current address to a WP. true if good, false if memory cannot be accessed.
    ocsd_datapath_resp_t processAtomRange(const ocsd_atm_val A, const char *pkt_msg, const waypoint_trace_t traceWPOp = TRACE_WAYPOINT, const ocsd_vaddr_t nextAddrMatch = 0);
    void checkPendingNacc(ocsd_datapath_resp_t &resp);

    uint8_t m_CSID; //!< Coresight trace ID for this decoder.

//** Other processor state;

    // trace decode FSM
    typedef enum {
        NO_SYNC,        //!< pre start trace - init state or after reset or overflow, loss of sync.
        WAIT_SYNC,      //!< waiting for sync packet.
        WAIT_ISYNC,     //!< waiting for isync packet after 1st ASYNC.
        DECODE_PKTS,    //!< processing input packet
        CONT_ISYNC,     //!< continue processing isync packet after WAIT. 
        CONT_ATOM,      //!< continue processing atom packet after WAIT.
        CONT_WPUP,      //!< continue processing WP update packet after WAIT.
        CONT_BRANCH,    //!< continue processing Branch packet after WAIT.
    } processor_state_t;

    processor_state_t m_curr_state;

    const bool processStateIsCont() const;

    // PE decode state - address and isa

    //! Structure to contain the PE addr and ISA state.
    typedef struct _ptm_pe_addr_state {
            ocsd_isa isa;              //!< current isa.
            ocsd_vaddr_t instr_addr;   //!< current address.
            bool valid;     //!< address valid - false if we need an address to continue decode.
    } ptm_pe_addr_state;

    ptm_pe_addr_state m_curr_pe_state;  //!< current instruction state for PTM decode.
    ocsd_pe_context m_pe_context;      //!< current context information

    // packet decode state
    bool m_need_isync;   //!< need context to continue
    
    ocsd_instr_info m_instr_info;  //!< instruction info for code follower - in address is the next to be decoded.

    bool m_mem_nacc_pending;    //!< need to output a memory access failure packet
    ocsd_vaddr_t m_nacc_addr;  //!< address of memory access failure
   
    bool m_i_sync_pe_ctxt;  //!< isync has pe context.

    PtmAtoms m_atoms;           //!< atoms to process in an atom packet

    TrcAddrReturnStack m_return_stack;  //!< trace return stack.
    
//** output element
    OcsdTraceElement m_output_elem;
};

inline const bool TrcPktDecodePtm::processStateIsCont() const
{
    return (bool)(m_curr_state >= CONT_ISYNC);
}

#endif // ARM_TRC_PKT_DECODE_PTM_H_INCLUDED

/* End of File trc_pkt_decode_ptm.h */
