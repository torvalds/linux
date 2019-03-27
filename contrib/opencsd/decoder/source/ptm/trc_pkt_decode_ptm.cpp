/*
 * \file       trc_pkt_decode_ptm.cpp
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

#include <sstream>
#include "opencsd/ptm/trc_pkt_decode_ptm.h"

#define DCD_NAME "DCD_PTM"

TrcPktDecodePtm::TrcPktDecodePtm()
    : TrcPktDecodeBase(DCD_NAME)
{
    initDecoder();
}

TrcPktDecodePtm::TrcPktDecodePtm(int instIDNum)
    : TrcPktDecodeBase(DCD_NAME,instIDNum)
{
    initDecoder();
}

TrcPktDecodePtm::~TrcPktDecodePtm()
{
}

/*********************** implementation packet decoding interface */

ocsd_datapath_resp_t TrcPktDecodePtm::processPacket()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    bool bPktDone = false;

    while(!bPktDone)
    {
        switch(m_curr_state)
        {
        case NO_SYNC:
            // no sync - output a no sync packet then transition to wait sync.
            m_output_elem.elem_type = OCSD_GEN_TRC_ELEM_NO_SYNC;
            resp = outputTraceElement(m_output_elem);
            m_curr_state = (m_curr_packet_in->getType() == PTM_PKT_A_SYNC) ? WAIT_ISYNC : WAIT_SYNC;
            bPktDone = true;
            break;

        case WAIT_SYNC:
            if(m_curr_packet_in->getType() == PTM_PKT_A_SYNC)
                m_curr_state = WAIT_ISYNC;
            bPktDone = true;
            break;

        case WAIT_ISYNC:
            if(m_curr_packet_in->getType() == PTM_PKT_I_SYNC)
                m_curr_state = DECODE_PKTS;
            else 
                bPktDone = true;
            break;

        case DECODE_PKTS:
            resp = decodePacket();
            bPktDone = true;
            break;

        default:
             // should only see these after a _WAIT resp - in flush handler 
        case CONT_ISYNC: 
        case CONT_ATOM:
            bPktDone = true;
            // throw a decoder error
            break;
        }
    }
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodePtm::onEOT()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    // shouldn't be any packets left to be processed - flush shoudl have done this.
    // just output the end of trace marker
    m_output_elem.setType(OCSD_GEN_TRC_ELEM_EO_TRACE);
    resp = outputTraceElement(m_output_elem);
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodePtm::onReset()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    resetDecoder();
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodePtm::onFlush()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    resp = contProcess();
    return resp;
}

// atom and isync packets can have multiple ouput packets that can be _WAITed mid stream.
ocsd_datapath_resp_t TrcPktDecodePtm::contProcess()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    switch(m_curr_state)
    { 
    case CONT_ISYNC:
        resp = processIsync();
        break;

    case CONT_ATOM:
        resp = processAtom();
        break;

    case CONT_WPUP:
        resp = processWPUpdate();
        break;        

    case CONT_BRANCH:
        resp = processBranch();
        break;

    default: break; // not a state that requires further processing
    }

    if(OCSD_DATA_RESP_IS_CONT(resp) && processStateIsCont())
        m_curr_state = DECODE_PKTS; // continue packet processing - assuming we have not degraded into an unsynced state.

    return resp;
}

ocsd_err_t TrcPktDecodePtm::onProtocolConfig()
{
    ocsd_err_t err = OCSD_OK;
    if(m_config == 0)
        return OCSD_ERR_NOT_INIT;

    // static config - copy of CSID for easy reference
    m_CSID = m_config->getTraceID();

    // handle return stack implementation
    if (m_config->hasRetStack())
    {
        m_return_stack.set_active(m_config->enaRetStack());
#ifdef TRC_RET_STACK_DEBUG
        m_return_stack.set_dbg_logger(this);
#endif
    }
    
    // config options affecting decode
    m_instr_info.pe_type.profile = m_config->coreProfile();
    m_instr_info.pe_type.arch = m_config->archVersion();
    m_instr_info.dsb_dmb_waypoints = m_config->dmsbWayPt() ? 1 : 0;
    return err;
}

/****************** local decoder routines */

void TrcPktDecodePtm::initDecoder()
{
    m_CSID = 0;
    m_instr_info.pe_type.profile = profile_Unknown;
    m_instr_info.pe_type.arch = ARCH_UNKNOWN;
    m_instr_info.dsb_dmb_waypoints = 0;
    resetDecoder();
}

void TrcPktDecodePtm::resetDecoder()
{
    m_curr_state = NO_SYNC;
    m_need_isync = true;    // need context to start.

    m_instr_info.isa = ocsd_isa_unknown;
    m_mem_nacc_pending = false;

    m_pe_context.ctxt_id_valid = 0;
    m_pe_context.bits64 = 0;
    m_pe_context.vmid_valid = 0;
    m_pe_context.exception_level = ocsd_EL_unknown;
    m_pe_context.security_level = ocsd_sec_secure;
    m_pe_context.el_valid = 0;
    
    m_curr_pe_state.instr_addr = 0x0;
    m_curr_pe_state.isa = ocsd_isa_unknown;
    m_curr_pe_state.valid = false;

    m_atoms.clearAll();
    m_output_elem.init();
}

ocsd_datapath_resp_t TrcPktDecodePtm::decodePacket()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    switch(m_curr_packet_in->getType())
    {
        // ignore these from trace o/p point of veiw
    case PTM_PKT_NOTSYNC:   
    case PTM_PKT_INCOMPLETE_EOT:
    case PTM_PKT_NOERROR:
        break;

        // bad / reserved packet - need to wait for next sync point
    case PTM_PKT_BAD_SEQUENCE:
    case PTM_PKT_RESERVED:
        m_curr_state = WAIT_SYNC;
        m_need_isync = true;    // need context to re-start.
        m_output_elem.setType(OCSD_GEN_TRC_ELEM_NO_SYNC);
        resp = outputTraceElement(m_output_elem);
        break;

        // packets we can ignore if in sync
    case PTM_PKT_A_SYNC:
    case PTM_PKT_IGNORE:
        break;

        // 
    case PTM_PKT_I_SYNC:
        resp = processIsync();
        break;

    case PTM_PKT_BRANCH_ADDRESS:
        resp = processBranch();
        break;

    case PTM_PKT_TRIGGER:
        m_output_elem.setType(OCSD_GEN_TRC_ELEM_EVENT);
        m_output_elem.setEvent(EVENT_TRIGGER, 0);
        resp = outputTraceElement(m_output_elem);
        break;

    case PTM_PKT_WPOINT_UPDATE:
        resp = processWPUpdate();
        break;

    case PTM_PKT_CONTEXT_ID:
        {
            bool bUpdate = true;  
            // see if this is a change
            if((m_pe_context.ctxt_id_valid) && (m_pe_context.context_id == m_curr_packet_in->context.ctxtID))
                bUpdate = false;
            if(bUpdate)
            {
                m_pe_context.context_id = m_curr_packet_in->context.ctxtID;
                m_pe_context.ctxt_id_valid = 1;
                m_output_elem.setType(OCSD_GEN_TRC_ELEM_PE_CONTEXT);
                m_output_elem.setContext(m_pe_context);
                resp = outputTraceElement(m_output_elem);
            }
        }        
        break;

    case PTM_PKT_VMID:
        {
            bool bUpdate = true;  
            // see if this is a change
            if((m_pe_context.vmid_valid) && (m_pe_context.vmid == m_curr_packet_in->context.VMID))
                bUpdate = false;
            if(bUpdate)
            {
                m_pe_context.vmid = m_curr_packet_in->context.VMID;
                m_pe_context.vmid_valid = 1;
                m_output_elem.setType(OCSD_GEN_TRC_ELEM_PE_CONTEXT);
                m_output_elem.setContext(m_pe_context);
                resp = outputTraceElement(m_output_elem);
            }
        }   
        break;

    case PTM_PKT_ATOM:
        if(m_curr_pe_state.valid)
        {
            m_atoms.initAtomPkt(m_curr_packet_in->getAtom(),m_index_curr_pkt);
            resp = processAtom();
        }
        break;

    case PTM_PKT_TIMESTAMP:
        m_output_elem.setType(OCSD_GEN_TRC_ELEM_TIMESTAMP);
        m_output_elem.timestamp = m_curr_packet_in->timestamp;
        if(m_curr_packet_in->cc_valid)
            m_output_elem.setCycleCount(m_curr_packet_in->cycle_count);
        resp = outputTraceElement(m_output_elem);
        break;

    case PTM_PKT_EXCEPTION_RET:
        m_output_elem.setType(OCSD_GEN_TRC_ELEM_EXCEPTION_RET);
        resp = outputTraceElement(m_output_elem);
        break;

    }
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodePtm::processIsync()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;

    // extract the I-Sync data if not re-entering after a _WAIT
    if(m_curr_state == DECODE_PKTS)
    {        
        m_curr_pe_state.instr_addr = m_curr_packet_in->getAddrVal();
        m_curr_pe_state.isa = m_curr_packet_in->getISA();
        m_curr_pe_state.valid = true;
        
        m_i_sync_pe_ctxt = m_curr_packet_in->ISAChanged();
        if(m_curr_packet_in->CtxtIDUpdated())
        {
            m_pe_context.context_id = m_curr_packet_in->getCtxtID();
            m_pe_context.ctxt_id_valid = 1;
            m_i_sync_pe_ctxt = true;
        }

        if(m_curr_packet_in->VMIDUpdated())
        {
            m_pe_context.vmid = m_curr_packet_in->getVMID();
            m_pe_context.vmid_valid = 1;
            m_i_sync_pe_ctxt = true;
        }
        m_pe_context.security_level = m_curr_packet_in->getNS() ? ocsd_sec_nonsecure : ocsd_sec_secure;
        
        if(m_need_isync || (m_curr_packet_in->iSyncReason() != iSync_Periodic))
        {
            m_output_elem.setType(OCSD_GEN_TRC_ELEM_TRACE_ON);
            m_output_elem.trace_on_reason = TRACE_ON_NORMAL;
            if(m_curr_packet_in->iSyncReason() == iSync_TraceRestartAfterOverflow)
                m_output_elem.trace_on_reason = TRACE_ON_OVERFLOW;
            else if(m_curr_packet_in->iSyncReason() == iSync_DebugExit)
                m_output_elem.trace_on_reason = TRACE_ON_EX_DEBUG;
            if(m_curr_packet_in->hasCC())
                m_output_elem.setCycleCount(m_curr_packet_in->getCCVal());
            resp = outputTraceElement(m_output_elem);           
        }
        else
        {
            // periodic - no output
            m_i_sync_pe_ctxt = false;
        }
        m_need_isync = false;   // got 1st Isync - can continue to process data.
        m_return_stack.flush();
    }
    
    if(m_i_sync_pe_ctxt && OCSD_DATA_RESP_IS_CONT(resp))
    {
        m_output_elem.setType(OCSD_GEN_TRC_ELEM_PE_CONTEXT);
        m_output_elem.setContext(m_pe_context);
        m_output_elem.setISA(m_curr_pe_state.isa);
        resp = outputTraceElement(m_output_elem); 
        m_i_sync_pe_ctxt = false;
    }

    // if wait and still stuff to process....
    if(OCSD_DATA_RESP_IS_WAIT(resp) && ( m_i_sync_pe_ctxt))
        m_curr_state = CONT_ISYNC;

    return resp;
}

// change of address and/or exception in program flow.
// implies E atom before the branch if none exception.
ocsd_datapath_resp_t TrcPktDecodePtm::processBranch()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;

    // initial pass - decoding packet.
    if(m_curr_state == DECODE_PKTS)
    {
        // specific behviour if this is an exception packet.
        if(m_curr_packet_in->isBranchExcepPacket())
        {
            // exception - record address and output exception packet.
            m_output_elem.setType(OCSD_GEN_TRC_ELEM_EXCEPTION);
            m_output_elem.exception_number = m_curr_packet_in->excepNum();
            m_output_elem.excep_ret_addr = 0;
            if(m_curr_pe_state.valid)
            {
                m_output_elem.excep_ret_addr = 1;
                m_output_elem.en_addr = m_curr_pe_state.instr_addr;
            }
            // could be an associated cycle count
            if(m_curr_packet_in->hasCC())
                m_output_elem.setCycleCount(m_curr_packet_in->getCCVal());

            // output the element
            resp = outputTraceElement(m_output_elem);
        }
        else
        {
            // branch address only - implies E atom - need to output a range element based on the atom.
            if(m_curr_pe_state.valid)
                resp = processAtomRange(ATOM_E,"BranchAddr");
        }

        // now set the branch address for the next time.
        m_curr_pe_state.isa = m_curr_packet_in->getISA();
        m_curr_pe_state.instr_addr = m_curr_packet_in->getAddrVal();
        m_curr_pe_state.valid = true;
    }

    // atom range may return with NACC pending 
    checkPendingNacc(resp);

    // if wait and still stuff to process....
    if(OCSD_DATA_RESP_IS_WAIT(resp) && ( m_mem_nacc_pending))
        m_curr_state = CONT_BRANCH;

    return resp;
}

// effectively completes a range prior to exception or after many bytes of trace (>4096)
//
ocsd_datapath_resp_t TrcPktDecodePtm::processWPUpdate()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;

    // if we need an address to run from then the WPUpdate will not form a range as 
    // we do not have a start point - still waiting for branch or other address packet
    if(m_curr_pe_state.valid)
    {
        // WP update implies atom - use E, we cannot be sure if the instruction passed its condition codes 
        // - though it doesn't really matter as it is not a branch so cannot change flow.
        resp = processAtomRange(ATOM_E,"WP update",TRACE_TO_ADDR_INCL,m_curr_packet_in->getAddrVal());
    }

    // atom range may return with NACC pending 
    checkPendingNacc(resp);

    // if wait and still stuff to process....
    if(OCSD_DATA_RESP_IS_WAIT(resp) && ( m_mem_nacc_pending))
        m_curr_state = CONT_WPUP;

    return resp;
}

// a single atom packet can result in multiple range outputs...need to be re-entrant in case we get a wait response.
// also need to handle nacc response from instruction walking routine
// 
ocsd_datapath_resp_t TrcPktDecodePtm::processAtom()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;

    // loop to process all the atoms in the packet
    while(m_atoms.numAtoms() && m_curr_pe_state.valid && OCSD_DATA_RESP_IS_CONT(resp))
    {
        resp = processAtomRange(m_atoms.getCurrAtomVal(),"atom");
        if(!m_curr_pe_state.valid)
            m_atoms.clearAll();
        else
            m_atoms.clearAtom();
    }

    // bad address may mean a nacc needs sending
    checkPendingNacc(resp);

    // if wait and still stuff to process....
    if(OCSD_DATA_RESP_IS_WAIT(resp) && ( m_mem_nacc_pending || m_atoms.numAtoms()))
        m_curr_state = CONT_ATOM;

    return resp;
}

 void TrcPktDecodePtm::checkPendingNacc(ocsd_datapath_resp_t &resp)
 {
    if(m_mem_nacc_pending && OCSD_DATA_RESP_IS_CONT(resp))
    {
        m_output_elem.setType(OCSD_GEN_TRC_ELEM_ADDR_NACC);
        m_output_elem.st_addr = m_nacc_addr;
        resp = outputTraceElementIdx(m_index_curr_pkt,m_output_elem);
        m_mem_nacc_pending = false;
    }
 }

// given an atom element - walk the code and output a range or mark nacc.
ocsd_datapath_resp_t TrcPktDecodePtm::processAtomRange(const ocsd_atm_val A, const char *pkt_msg, const waypoint_trace_t traceWPOp /*= TRACE_WAYPOINT*/, const ocsd_vaddr_t nextAddrMatch /*= 0*/)
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    bool bWPFound = false;
    std::ostringstream oss;

    m_instr_info.instr_addr = m_curr_pe_state.instr_addr;
    m_instr_info.isa = m_curr_pe_state.isa;

    ocsd_err_t err = traceInstrToWP(bWPFound,traceWPOp,nextAddrMatch);
    if(err != OCSD_OK)
    {
        if(err == OCSD_ERR_UNSUPPORTED_ISA)
        {
                m_curr_pe_state.valid = false; // need a new address packet
                oss << "Warning: unsupported instruction set processing " << pkt_msg << " packet.";
                LogError(ocsdError(OCSD_ERR_SEV_WARN,err,m_index_curr_pkt,m_CSID,oss.str()));  
                // wait for next address
                return OCSD_RESP_WARN_CONT;
        }
        else
        {
            resp = OCSD_RESP_FATAL_INVALID_DATA;
            oss << "Error processing " << pkt_msg << " packet.";
            LogError(ocsdError(OCSD_ERR_SEV_ERROR,err,m_index_curr_pkt,m_CSID,oss.str()));  
            return resp;
        }
    }
    
    if(bWPFound)
    {
        //  save recorded next instuction address
        ocsd_vaddr_t nextAddr = m_instr_info.instr_addr;

        // action according to waypoint type and atom value
        switch(m_instr_info.type)
        {
        case OCSD_INSTR_BR:
            if (A == ATOM_E)
            {
                m_instr_info.instr_addr = m_instr_info.branch_addr;
                if (m_instr_info.is_link)
                    m_return_stack.push(nextAddr,m_instr_info.isa);
            }
            break;

            // For PTM -> branch addresses imply E atom, N atom does not need address (return stack will require this)
        case OCSD_INSTR_BR_INDIRECT:
            if (A == ATOM_E)
            {
                // atom on indirect branch - either implied E from a branch address packet, or return stack if active.

                // indirect branch taken - need new address -if the current packet is a branch address packet this will be sorted.
                m_curr_pe_state.valid = false; 

                // if return stack and the incoming packet is an atom.
                if (m_return_stack.is_active() && (m_curr_packet_in->getType() == PTM_PKT_ATOM))
                {
                    // we have an E atom packet and return stack value - set address from return stack
                    m_instr_info.instr_addr = m_return_stack.pop(m_instr_info.next_isa);

                    if (m_return_stack.overflow())
                    {
                        resp = OCSD_RESP_FATAL_INVALID_DATA;
                        oss << "Return stack error processing " << pkt_msg << " packet.";
                        LogError(ocsdError(OCSD_ERR_SEV_ERROR, OCSD_ERR_RET_STACK_OVERFLOW, m_index_curr_pkt, m_CSID, oss.str()));
                        return resp;
                    }
                    else
                        m_curr_pe_state.valid = true; 
                }
                if(m_instr_info.is_link)
                    m_return_stack.push(nextAddr, m_instr_info.isa);
            }
            break;
        }
        
        m_output_elem.setType(OCSD_GEN_TRC_ELEM_INSTR_RANGE);
        m_output_elem.setLastInstrInfo((A == ATOM_E),m_instr_info.type, m_instr_info.sub_type);
        m_output_elem.setISA(m_curr_pe_state.isa);
        if(m_curr_packet_in->hasCC())
            m_output_elem.setCycleCount(m_curr_packet_in->getCCVal());
        resp = outputTraceElementIdx(m_index_curr_pkt,m_output_elem);

        m_curr_pe_state.instr_addr = m_instr_info.instr_addr;
        m_curr_pe_state.isa = m_instr_info.next_isa;
    }
    else
    {
        // no waypoint - likely inaccessible memory range.
        m_curr_pe_state.valid = false; // need an address update 

        if(m_output_elem.st_addr != m_output_elem.en_addr)
        {
            // some trace before we were out of memory access range
            m_output_elem.setType(OCSD_GEN_TRC_ELEM_INSTR_RANGE);
            m_output_elem.setLastInstrInfo(true,m_instr_info.type, m_instr_info.sub_type);
            m_output_elem.setISA(m_curr_pe_state.isa);
            resp = outputTraceElementIdx(m_index_curr_pkt,m_output_elem);
        }
    }
    return resp;
}

ocsd_err_t TrcPktDecodePtm::traceInstrToWP(bool &bWPFound, const waypoint_trace_t traceWPOp /*= TRACE_WAYPOINT*/, const ocsd_vaddr_t nextAddrMatch /*= 0*/)
{
    uint32_t opcode;
    uint32_t bytesReq;
    ocsd_err_t err = OCSD_OK;
    ocsd_vaddr_t curr_op_address;

    ocsd_mem_space_acc_t mem_space = (m_pe_context.security_level == ocsd_sec_secure) ? OCSD_MEM_SPACE_S : OCSD_MEM_SPACE_N;

    m_output_elem.st_addr = m_output_elem.en_addr = m_instr_info.instr_addr;

    bWPFound = false;

    while(!bWPFound && !m_mem_nacc_pending)
    {
        // start off by reading next opcode;
        bytesReq = 4;
        curr_op_address = m_instr_info.instr_addr;  // save the start address for the current opcode
        err = accessMemory(m_instr_info.instr_addr,mem_space,&bytesReq,(uint8_t *)&opcode);
        if(err != OCSD_OK) break;

        if(bytesReq == 4) // got data back
        {
            m_instr_info.opcode = opcode;
            err = instrDecode(&m_instr_info);
            if(err != OCSD_OK) break;

            // increment address - may be adjusted by direct branch value later
            m_instr_info.instr_addr += m_instr_info.instr_size;

            // update the range decoded address in the output packet.
            m_output_elem.en_addr = m_instr_info.instr_addr;

            m_output_elem.last_i_type = m_instr_info.type;
            // either walking to match the next instruction address or a real waypoint
            if(traceWPOp != TRACE_WAYPOINT)
            {
                if(traceWPOp == TRACE_TO_ADDR_EXCL)
                    bWPFound = (m_output_elem.en_addr == nextAddrMatch);
                else
                    bWPFound = (curr_op_address == nextAddrMatch);
            }
            else
                bWPFound = (m_instr_info.type != OCSD_INSTR_OTHER);
        }
        else
        {
            // not enough memory accessible.
            m_mem_nacc_pending = true;
            m_nacc_addr = m_instr_info.instr_addr;
        }
    }
    return err;
}

/* End of File trc_pkt_decode_ptm.cpp */
