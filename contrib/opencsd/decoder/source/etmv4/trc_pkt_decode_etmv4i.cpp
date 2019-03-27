/*
 * \file       trc_pkt_decode_etmv4i.cpp
 * \brief      OpenCSD : ETMv4 decoder
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

#include "opencsd/etmv4/trc_pkt_decode_etmv4i.h"

#include "common/trc_gen_elem.h"


#define DCD_NAME "DCD_ETMV4"

static const uint32_t ETMV4_SUPPORTED_DECODE_OP_FLAGS = OCSD_OPFLG_PKTDEC_COMMON;

TrcPktDecodeEtmV4I::TrcPktDecodeEtmV4I()
    : TrcPktDecodeBase(DCD_NAME)
{
    initDecoder();
}

TrcPktDecodeEtmV4I::TrcPktDecodeEtmV4I(int instIDNum)
    : TrcPktDecodeBase(DCD_NAME,instIDNum)
{
    initDecoder();
}

TrcPktDecodeEtmV4I::~TrcPktDecodeEtmV4I()   
{
}

/*********************** implementation packet decoding interface */

ocsd_datapath_resp_t TrcPktDecodeEtmV4I::processPacket()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    bool bPktDone = false;

    while(!bPktDone)
    {
        switch (m_curr_state)
        {
        case NO_SYNC:
            // output the initial not synced packet to the sink
            m_output_elem.setType(OCSD_GEN_TRC_ELEM_NO_SYNC);
            resp = outputTraceElement(m_output_elem);
            m_curr_state = WAIT_SYNC;
            // fall through to check if the current packet is the async we are waiting for.
            break;

        case WAIT_SYNC:
            if(m_curr_packet_in->getType() == ETM4_PKT_I_ASYNC)
                m_curr_state = WAIT_TINFO;
            bPktDone = true;
            break;

        case WAIT_TINFO:
            m_need_ctxt = true;
            m_need_addr = true;
            if(m_curr_packet_in->getType() == ETM4_PKT_I_TRACE_INFO)
            {
                doTraceInfoPacket();
                m_curr_state = DECODE_PKTS;
                m_return_stack.flush();
            }
            bPktDone = true;
            break;

        case DECODE_PKTS:
            resp = decodePacket(bPktDone);  // this may change the state to commit elem;
            break;

        case COMMIT_ELEM:
            resp = commitElements(bPktDone); // this will change the state to DECODE_PKTS once all elem committed.
            break;

        }
    }
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodeEtmV4I::onEOT()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    m_flush_EOT = true;
    resp = flushEOT();
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodeEtmV4I::onReset()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    resetDecoder();
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodeEtmV4I::onFlush()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;

    // continue exception processing (can't go through processPacket as elements no longer on stack)
    if(m_excep_proc != EXCEP_POP)
        resp = processException();
    // continue ongoing output operations on comitted elements.
    else if(m_curr_state == COMMIT_ELEM)
        resp = processPacket(); 
    // continue flushing at end of trace
    else if(m_flush_EOT)
        resp = flushEOT();
    return resp;
}

ocsd_err_t TrcPktDecodeEtmV4I::onProtocolConfig()
{
    ocsd_err_t err = OCSD_OK;

    // set some static config elements
    m_CSID = m_config->getTraceID();
    m_max_spec_depth = m_config->MaxSpecDepth();
    m_p0_key_max = m_config->P0_Key_Max();
    m_cond_key_max_incr = m_config->CondKeyMaxIncr();

    // set up static trace instruction decode elements
    m_instr_info.dsb_dmb_waypoints = 0;
    m_instr_info.pe_type.arch = m_config->archVersion();
    m_instr_info.pe_type.profile = m_config->coreProfile();

    m_IASize64 = (m_config->iaSizeMax() == 64);

    if (m_config->enabledRetStack())
    {
        m_return_stack.set_active(true);
#ifdef TRC_RET_STACK_DEBUG
        m_return_stack.set_dbg_logger(this);
#endif
    }

    // check config compatible with current decoder support level.
    // at present no data trace, no spec depth, no return stack, no QE
    // Remove these checks as support is added.
    if(m_max_spec_depth != 0)
    {
        err = OCSD_ERR_HW_CFG_UNSUPP;
        LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_HW_CFG_UNSUPP,"ETMv4 instruction decode : None-zero speculation depth not supported"));
    }
    else if(m_config->enabledDataTrace())
    {
        err = OCSD_ERR_HW_CFG_UNSUPP;
        LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_HW_CFG_UNSUPP,"ETMv4 instruction decode : Data trace elements not supported"));
    }
    else if(m_config->enabledLSP0Trace())
    {
        err = OCSD_ERR_HW_CFG_UNSUPP;
        LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_HW_CFG_UNSUPP,"ETMv4 instruction decode : LSP0 elements not supported."));
    }
    else if(m_config->enabledCondITrace() != EtmV4Config::COND_TR_DIS)
    {
        err = OCSD_ERR_HW_CFG_UNSUPP;
        LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_HW_CFG_UNSUPP,"ETMv4 instruction decode : Trace on conditional non-branch elements not supported."));
    }
    else if(m_config->enabledQE())
    {
        err = OCSD_ERR_HW_CFG_UNSUPP;
        LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_HW_CFG_UNSUPP,"ETMv4 instruction decode : Trace using Q elements not supported."));
    }
    return err;
}


/************* local decode methods */
void TrcPktDecodeEtmV4I::initDecoder()
{
    // set the operational modes supported.
    m_supported_op_flags = ETMV4_SUPPORTED_DECODE_OP_FLAGS;

    /* init elements that get set by config */
    m_max_spec_depth = 0;
    m_p0_key_max = 0;
    m_CSID = 0;
    m_cond_key_max_incr = 0;
    m_IASize64 = false;

    // reset decoder state to unsynced
    resetDecoder();
}

void TrcPktDecodeEtmV4I::resetDecoder()
{
    m_curr_state = NO_SYNC;
    m_timestamp = 0;
    m_context_id = 0;              
    m_vmid_id = 0;                 
    m_is_secure = true;
    m_is_64bit = false;
    m_cc_threshold = 0;
    m_curr_spec_depth = 0;
    m_p0_key = 0;
    m_cond_c_key = 0;
    m_cond_r_key = 0;
    m_need_ctxt = true;
    m_need_addr = true;
    m_except_pending_addr = false;
    m_mem_nacc_pending = false;
    m_prev_overflow = false;
    m_P0_stack.delete_all();
    m_output_elem.init();
    m_excep_proc = EXCEP_POP;
    m_flush_EOT = false;
}

// this function can output an immediate generic element if this covers the complete packet decode, 
// or stack P0 and other elements for later processing on commit or cancel.
ocsd_datapath_resp_t TrcPktDecodeEtmV4I::decodePacket(bool &Complete)
{
    bool bAllocErr = false;
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    Complete = true;
    bool is_addr = false;
    bool is_except = false;
    
    switch(m_curr_packet_in->getType())
    {
    case ETM4_PKT_I_ASYNC: // nothing to do with this packet.
        break;

    case ETM4_PKT_I_TRACE_INFO:
        // skip subsequent TInfo packets.
        m_return_stack.flush();
        break;

    case ETM4_PKT_I_TRACE_ON:
        {
            if (m_P0_stack.createParamElemNoParam(P0_TRC_ON, false, m_curr_packet_in->getType(), m_index_curr_pkt) == 0)
                bAllocErr = true;
        }
        break;

    case ETM4_PKT_I_OVERFLOW:
        {
            if (m_P0_stack.createParamElemNoParam(P0_OVERFLOW, false, m_curr_packet_in->getType(), m_index_curr_pkt) == 0)
                bAllocErr = true;
        }
        break;

    case ETM4_PKT_I_ATOM_F1:
    case ETM4_PKT_I_ATOM_F2:
    case ETM4_PKT_I_ATOM_F3:
    case ETM4_PKT_I_ATOM_F4:
    case ETM4_PKT_I_ATOM_F5:
    case ETM4_PKT_I_ATOM_F6:
        {
            if (m_P0_stack.createAtomElem(m_curr_packet_in->getType(), m_index_curr_pkt, m_curr_packet_in->getAtom()) == 0)
                bAllocErr = true;
            else
                m_curr_spec_depth += m_curr_packet_in->getAtom().num;
        }
        break;

    case ETM4_PKT_I_CTXT:
        {
            if (m_P0_stack.createContextElem(m_curr_packet_in->getType(), m_index_curr_pkt, m_curr_packet_in->getContext()) == 0)
                bAllocErr = true;
        }
        break;

    case ETM4_PKT_I_ADDR_MATCH:
        {
            etmv4_addr_val_t addr;

            addr.val = m_curr_packet_in->getAddrVal();
            addr.isa = m_curr_packet_in->getAddrIS();
            if (m_P0_stack.createAddrElem(m_curr_packet_in->getType(), m_index_curr_pkt, addr) == 0)
                bAllocErr = true;
            is_addr = true;
        }
        break;

    case ETM4_PKT_I_ADDR_CTXT_L_64IS0:
    case ETM4_PKT_I_ADDR_CTXT_L_64IS1:
    case ETM4_PKT_I_ADDR_CTXT_L_32IS0:
    case ETM4_PKT_I_ADDR_CTXT_L_32IS1:    
        {
            if (m_P0_stack.createContextElem(m_curr_packet_in->getType(), m_index_curr_pkt, m_curr_packet_in->getContext()) == 0)
                bAllocErr = true;
        }
    case ETM4_PKT_I_ADDR_L_32IS0:
    case ETM4_PKT_I_ADDR_L_32IS1:
    case ETM4_PKT_I_ADDR_L_64IS0:
    case ETM4_PKT_I_ADDR_L_64IS1:
    case ETM4_PKT_I_ADDR_S_IS0:
    case ETM4_PKT_I_ADDR_S_IS1:
        {
            etmv4_addr_val_t addr;

            addr.val = m_curr_packet_in->getAddrVal();
            addr.isa = m_curr_packet_in->getAddrIS();
            if (m_P0_stack.createAddrElem(m_curr_packet_in->getType(), m_index_curr_pkt, addr) == 0)
                bAllocErr = true;
            is_addr = true;
        }
        break;

    // Exceptions
    case ETM4_PKT_I_EXCEPT:
         {
            if (m_P0_stack.createExceptElem(m_curr_packet_in->getType(), m_index_curr_pkt, 
                                            (m_curr_packet_in->exception_info.addr_interp == 0x2), 
                                            m_curr_packet_in->exception_info.exceptionType) == 0)
                bAllocErr = true;
            else
            {
                m_except_pending_addr = true;  // wait for following packets before marking for commit.
                is_except = true;
            }
        }
        break;

    case ETM4_PKT_I_EXCEPT_RTN:
        {
            // P0 element if V7M profile.
            bool bV7MProfile = (m_config->archVersion() == ARCH_V7) && (m_config->coreProfile() == profile_CortexM);
            if (m_P0_stack.createParamElemNoParam(P0_EXCEP_RET, bV7MProfile, m_curr_packet_in->getType(), m_index_curr_pkt) == 0)
                bAllocErr = true;
        }
        break;

    // event trace
    case ETM4_PKT_I_EVENT:
        {
            std::vector<uint32_t> params = { 0 };
            params[0] = (uint32_t)m_curr_packet_in->event_val;
            if (m_P0_stack.createParamElem(P0_EVENT, false, m_curr_packet_in->getType(), m_index_curr_pkt, params) == 0)
                bAllocErr = true;

        }
        break;

    /* cycle count packets */
    case ETM4_PKT_I_CCNT_F1:
    case ETM4_PKT_I_CCNT_F2:
    case ETM4_PKT_I_CCNT_F3:
        {
            std::vector<uint32_t> params = { 0 };
            params[0] = m_curr_packet_in->getCC();
            if (m_P0_stack.createParamElem(P0_EVENT, false, m_curr_packet_in->getType(), m_index_curr_pkt, params) == 0)
                bAllocErr = true;

        }
        break;

    // timestamp
    case ETM4_PKT_I_TIMESTAMP:
        {
            bool bTSwithCC = m_config->enabledCCI();
            uint64_t ts = m_curr_packet_in->getTS();
            std::vector<uint32_t> params = { 0, 0, 0 };
            params[0] = (uint32_t)(ts & 0xFFFFFFFF);
            params[1] = (uint32_t)((ts >> 32) & 0xFFFFFFFF);
            if (bTSwithCC)
                params[2] = m_curr_packet_in->getCC();
            if (m_P0_stack.createParamElem(P0_EVENT, false, m_curr_packet_in->getType(), m_index_curr_pkt, params) == 0)
                bAllocErr = true;

        }
        break;

    case ETM4_PKT_I_BAD_SEQUENCE:
        resp = handleBadPacket("Bad byte sequence in packet.");
        break;

    case ETM4_PKT_I_BAD_TRACEMODE:
        resp = handleBadPacket("Invalid packet type for trace mode.");
        break;

    case ETM4_PKT_I_RESERVED:
        resp = handleBadPacket("Reserved packet header");
        break;

    /*** presently unsupported packets ***/
    /* conditional instruction tracing */
    case ETM4_PKT_I_COND_FLUSH:
    case ETM4_PKT_I_COND_I_F1:
    case ETM4_PKT_I_COND_I_F2:
    case ETM4_PKT_I_COND_I_F3:
    case ETM4_PKT_I_COND_RES_F1:
    case ETM4_PKT_I_COND_RES_F2:
    case ETM4_PKT_I_COND_RES_F3:
    case ETM4_PKT_I_COND_RES_F4:
    // speculation 
    case ETM4_PKT_I_CANCEL_F1:
    case ETM4_PKT_I_CANCEL_F2:
    case ETM4_PKT_I_CANCEL_F3:
    case ETM4_PKT_I_COMMIT:
    case ETM4_PKT_I_MISPREDICT:
    case ETM4_PKT_I_DISCARD:
    // data synchronisation markers
    case ETM4_PKT_I_NUM_DS_MKR:
    case ETM4_PKT_I_UNNUM_DS_MKR:
    /* Q packets */
    case ETM4_PKT_I_Q:
        resp = OCSD_RESP_FATAL_INVALID_DATA;
        LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_BAD_DECODE_PKT,"Unsupported packet type."));
        break;

    default:
        // any other packet - bad packet error
        resp = OCSD_RESP_FATAL_INVALID_DATA;
        LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_BAD_DECODE_PKT,"Unknown packet type."));
        break;

    }

    // we need to wait for following address after exception 
    // - work out if we have seen enough here...
    if(m_except_pending_addr && !is_except)
    {
        m_except_pending_addr = false;  //next packet has to be an address
        // exception packet sequence complete
        if(is_addr)
        {
            m_curr_spec_depth++;   // exceptions are P0 elements so up the spec depth to commit if needed.
        }
        else
        {
            resp = OCSD_RESP_FATAL_INVALID_DATA;
            LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_BAD_DECODE_PKT,"Expected Address packet to follow exception packet."));
        }
    }

    if(bAllocErr)
    {
        resp = OCSD_RESP_FATAL_SYS_ERR;
        LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_MEM,"Memory allocation error."));       
    }
    else if(m_curr_spec_depth > m_max_spec_depth)
    {
        // auto commit anything above max spec depth 
        // (this will auto commit anything if spec depth not supported!)
        m_P0_commit = m_curr_spec_depth - m_max_spec_depth;
        m_curr_state = COMMIT_ELEM;
        Complete = false;   // force the processing of the commit elements.        
    }
    return resp;
}

void TrcPktDecodeEtmV4I::doTraceInfoPacket()
{
    m_trace_info = m_curr_packet_in->getTraceInfo();
    m_cc_threshold = m_curr_packet_in->getCCThreshold();
    m_p0_key = m_curr_packet_in->getP0Key();
    m_curr_spec_depth = m_curr_packet_in->getCurrSpecDepth();
}

/*
 * Walks through the element stack, processing from oldest element to the newest, 
   according to the number of P0 elements that need committing.
 */
ocsd_datapath_resp_t TrcPktDecodeEtmV4I::commitElements(bool &Complete)
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    bool bPause = false;    // pause commit operation 
    bool bPopElem = true;       // do we remove the element from the stack (multi atom elements may need to stay!)
    int num_commit_req = m_P0_commit;

    Complete = true; // assume we exit due to completion of commit operation

    TrcStackElem *pElem = 0;    // stacked element pointer

    while(m_P0_commit && !bPause)
    {
        if(m_P0_stack.size() > 0)
        {
            pElem = m_P0_stack.back();  // get oldest element
            
            switch(pElem->getP0Type())
            {
            // indicates a trace restart - beginning of trace or discontinuiuty
            case P0_TRC_ON:
                m_output_elem.setType(OCSD_GEN_TRC_ELEM_TRACE_ON);
                m_output_elem.trace_on_reason = m_prev_overflow ? TRACE_ON_OVERFLOW : TRACE_ON_NORMAL;
                m_prev_overflow = false;
                resp = outputTraceElementIdx(pElem->getRootIndex(),m_output_elem);
                m_return_stack.flush();
                break;

            case P0_ADDR:
                {
                TrcStackElemAddr *pAddrElem = dynamic_cast<TrcStackElemAddr *>(pElem);
                m_return_stack.clear_pop_pending(); // address removes the need to pop the indirect address target from the stack
                if(pAddrElem)
                {
                    SetInstrInfoInAddrISA(pAddrElem->getAddr().val, pAddrElem->getAddr().isa);
                    m_need_addr = false;
                }
                }
                break;

            case P0_CTXT:
                {
                TrcStackElemCtxt *pCtxtElem = dynamic_cast<TrcStackElemCtxt *>(pElem);
                if(pCtxtElem)
                {
                    etmv4_context_t ctxt = pCtxtElem->getContext();
                    // check this is an updated context
                    if(ctxt.updated)
                    {
                        updateContext(pCtxtElem);

                        m_output_elem.setType(OCSD_GEN_TRC_ELEM_PE_CONTEXT);
                        resp = outputTraceElementIdx(pElem->getRootIndex(),m_output_elem);
                    }
                }
                }
                break;

            case P0_EVENT:
                {
                TrcStackElemParam *pParamElem = dynamic_cast<TrcStackElemParam *>(pElem);
                if(pParamElem)
                    resp = this->outputEvent(pParamElem);
                }
                break;

            case P0_TS:
                {
                TrcStackElemParam *pParamElem = dynamic_cast<TrcStackElemParam *>(pElem);
                if(pParamElem)
                    resp = outputTS(pParamElem,false);
                }
                break;

            case P0_CC:
                {
                TrcStackElemParam *pParamElem = dynamic_cast<TrcStackElemParam *>(pElem);
                if(pParamElem)
                    resp = outputCC(pParamElem);
                }
                break;

            case P0_TS_CC:
                {
                TrcStackElemParam *pParamElem = dynamic_cast<TrcStackElemParam *>(pElem);
                if(pParamElem)
                    resp = outputTS(pParamElem,true);
                }
                break;

            case P0_OVERFLOW:
                m_prev_overflow = true;
                break;

            case P0_ATOM:
                {
                TrcStackElemAtom *pAtomElem = dynamic_cast<TrcStackElemAtom *>(pElem);

                if(pAtomElem)
                {
                    bool bContProcess = true;
                    while(!pAtomElem->isEmpty() && m_P0_commit && bContProcess)
                    {
                        ocsd_atm_val atom = pAtomElem->commitOldest();

                        // check if prev atom left us an indirect address target on the return stack
                        if ((resp = returnStackPop()) != OCSD_RESP_CONT)
                            break;

                        // if address and context do instruction trace follower.
                        // otherwise skip atom and reduce committed elements
                        if(!m_need_ctxt && !m_need_addr)
                        {
                            resp = processAtom(atom,bContProcess);
                        }
                        m_P0_commit--; // mark committed 
                    }
                    if(!pAtomElem->isEmpty())   
                        bPopElem = false;   // don't remove if still atoms to process.
                }
                }
                break;

            case P0_EXCEP:
                // check if prev atom left us an indirect address target on the return stack
                if ((resp = returnStackPop()) != OCSD_RESP_CONT)
                    break;

                m_excep_proc = EXCEP_POP;   // set state in case we need to stop part way through
                resp = processException();  // output trace + exception elements.
                m_P0_commit--;
                break;

            case P0_EXCEP_RET:
                m_output_elem.setType(OCSD_GEN_TRC_ELEM_EXCEPTION_RET);
                resp = outputTraceElementIdx(pElem->getRootIndex(),m_output_elem);
                if(pElem->isP0()) // are we on a core that counts ERET as P0?
                    m_P0_commit--;
                break;
            }

            if(bPopElem)
                m_P0_stack.delete_back();  // remove element from stack;

            // if response not continue, then break out of the loop.
            if(!OCSD_DATA_RESP_IS_CONT(resp))
            {
                bPause = true;
            }
        }
        else
        {
            // too few elements for commit operation - decode error.
            ocsd_trc_index_t err_idx = 0;
            if(pElem)
                err_idx = pElem->getRootIndex();
              
            resp = OCSD_RESP_FATAL_INVALID_DATA;
            LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_COMMIT_PKT_OVERRUN,err_idx,m_CSID,"Not enough elements to commit"));
            bPause = true;
        }
    }

    // done all elements - need more packets.
    if(m_P0_commit == 0)    
        m_curr_state = DECODE_PKTS;

    // reduce the spec depth by number of comitted elements
    m_curr_spec_depth -= (num_commit_req-m_P0_commit);
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodeEtmV4I::returnStackPop()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    ocsd_isa nextISA;
    
    if (m_return_stack.pop_pending())
    {
        ocsd_vaddr_t popAddr = m_return_stack.pop(nextISA);
        if (m_return_stack.overflow())
        {
            resp = OCSD_RESP_FATAL_INVALID_DATA;
            LogError(ocsdError(OCSD_ERR_SEV_ERROR, OCSD_ERR_RET_STACK_OVERFLOW, "Trace Return Stack Overflow."));
        }
        else
        {
            m_instr_info.instr_addr = popAddr;
            m_instr_info.isa = nextISA;
            m_need_addr = false;
        }
    }
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodeEtmV4I::flushEOT()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    if(m_flush_EOT)
    {
        TrcStackElem *pElem = 0;
        while(OCSD_DATA_RESP_IS_CONT(resp) && (m_P0_stack.size() > 0))
        {
            // scan for outstanding events, TS and CC, before any outstanding
            // P0 commit elements.
            pElem = m_P0_stack.back();
            
            switch(pElem->getP0Type())
            {
                // clear stack and stop
            case P0_UNKNOWN:
            case P0_ATOM:
            case P0_TRC_ON:
            case P0_EXCEP:
            case P0_EXCEP_RET:
            case P0_OVERFLOW:
                m_P0_stack.delete_all();
                break;

                //skip
            case P0_ADDR:
            case P0_CTXT:
                break;

                // output
            case P0_EVENT:
                {
                TrcStackElemParam *pParamElem = dynamic_cast<TrcStackElemParam *>(pElem);
                if(pParamElem)
                    resp = this->outputEvent(pParamElem);
                }
                break;

            case P0_TS:
                {
                TrcStackElemParam *pParamElem = dynamic_cast<TrcStackElemParam *>(pElem);
                if(pParamElem)
                    resp = outputTS(pParamElem,false);
                }
                break;

            case P0_CC:
                {
                TrcStackElemParam *pParamElem = dynamic_cast<TrcStackElemParam *>(pElem);
                if(pParamElem)
                    resp = outputCC(pParamElem);
                }
                break;

            case P0_TS_CC:
                {
                TrcStackElemParam *pParamElem = dynamic_cast<TrcStackElemParam *>(pElem);
                if(pParamElem)
                    resp = outputTS(pParamElem,true);
                }
                break;
            }
            m_P0_stack.delete_back();
        }

        if(OCSD_DATA_RESP_IS_CONT(resp) && (m_P0_stack.size() == 0))
        {
            m_output_elem.setType(OCSD_GEN_TRC_ELEM_EO_TRACE);
            resp = outputTraceElement(m_output_elem);
            m_flush_EOT = false;
        }
    }
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodeEtmV4I::outputCC(TrcStackElemParam *pParamElem)
{
    m_output_elem.setType(OCSD_GEN_TRC_ELEM_CYCLE_COUNT);
    m_output_elem.cycle_count = pParamElem->getParam(0);
    return outputTraceElementIdx(pParamElem->getRootIndex(),m_output_elem);
}

ocsd_datapath_resp_t TrcPktDecodeEtmV4I::outputTS(TrcStackElemParam *pParamElem, bool withCC)
{
    m_output_elem.setType(OCSD_GEN_TRC_ELEM_TIMESTAMP);
    m_output_elem.timestamp = (uint64_t)(pParamElem->getParam(0)) | (((uint64_t)pParamElem->getParam(1)) << 32);
    if(withCC)
        m_output_elem.setCycleCount(pParamElem->getParam(2));
    return outputTraceElementIdx(pParamElem->getRootIndex(),m_output_elem);
}

ocsd_datapath_resp_t TrcPktDecodeEtmV4I::outputEvent(TrcStackElemParam *pParamElem)
{
    m_output_elem.setType(OCSD_GEN_TRC_ELEM_EVENT);
    m_output_elem.trace_event.ev_type = EVENT_NUMBERED;
    m_output_elem.trace_event.ev_number = pParamElem->getParam(0);
    return outputTraceElementIdx(pParamElem->getRootIndex(),m_output_elem);
}

ocsd_datapath_resp_t TrcPktDecodeEtmV4I::processAtom(const ocsd_atm_val atom, bool &bCont)
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    TrcStackElem *pElem = m_P0_stack.back();  // get the atom element
    bool bWPFound = false;
    ocsd_err_t err;
    bCont = true;

    err = traceInstrToWP(bWPFound);
    if(err != OCSD_OK)
    {
        if(err == OCSD_ERR_UNSUPPORTED_ISA)
        {
             m_need_addr = true;
             m_need_ctxt = true;
             LogError(ocsdError(OCSD_ERR_SEV_WARN,err,pElem->getRootIndex(),m_CSID,"Warning: unsupported instruction set processing atom packet."));  
             // wait for next context
             return resp;
        }
        else
        {
            bCont = false;
            resp = OCSD_RESP_FATAL_INVALID_DATA;
            LogError(ocsdError(OCSD_ERR_SEV_ERROR,err,pElem->getRootIndex(),m_CSID,"Error processing atom packet."));  
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
            if (atom == ATOM_E)
            {
                m_instr_info.instr_addr = m_instr_info.branch_addr;
                if (m_instr_info.is_link)
                    m_return_stack.push(nextAddr, m_instr_info.isa);

            }
            break;

        case OCSD_INSTR_BR_INDIRECT:
            if (atom == ATOM_E)
            {
                m_need_addr = true; // indirect branch taken - need new address.
                if (m_instr_info.is_link)
                    m_return_stack.push(nextAddr,m_instr_info.isa);
                m_return_stack.set_pop_pending();  // need to know next packet before we know what is to happen
            }
            break;
        }
        m_output_elem.setType(OCSD_GEN_TRC_ELEM_INSTR_RANGE);
        m_output_elem.setLastInstrInfo((atom == ATOM_E),m_instr_info.type, m_instr_info.sub_type);
        m_output_elem.setISA(m_instr_info.isa);
        resp = outputTraceElementIdx(pElem->getRootIndex(),m_output_elem);

    }
    else
    {
        // no waypoint - likely inaccessible memory range.
        m_need_addr = true; // need an address update 

        if(m_output_elem.st_addr != m_output_elem.en_addr)
        {
            // some trace before we were out of memory access range
            m_output_elem.setType(OCSD_GEN_TRC_ELEM_INSTR_RANGE);
            m_output_elem.setLastInstrInfo(true,m_instr_info.type, m_instr_info.sub_type);
            m_output_elem.setISA(m_instr_info.isa);
            resp = outputTraceElementIdx(pElem->getRootIndex(),m_output_elem);
        }

        if(m_mem_nacc_pending && OCSD_DATA_RESP_IS_CONT(resp))
        {
            m_output_elem.setType(OCSD_GEN_TRC_ELEM_ADDR_NACC);
            m_output_elem.st_addr = m_nacc_addr;
            resp = outputTraceElementIdx(pElem->getRootIndex(),m_output_elem);
            m_mem_nacc_pending = false;
        }
    }

    if(!OCSD_DATA_RESP_IS_CONT(resp))
        bCont = false;

    return resp;
}

// Exception processor
ocsd_datapath_resp_t  TrcPktDecodeEtmV4I::processException()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT; 
    bool excep_implied_P0 = false;          //!< exception implies P0

    if(m_excep_proc == EXCEP_POP)
    {
        TrcStackElemExcept *pExceptElem = dynamic_cast<TrcStackElemExcept *>(m_P0_stack.back());  // get the exception element
        TrcStackElemAddr *pAddressElem = 0;
        TrcStackElemCtxt *pCtxtElem = 0;
        TrcStackElem *pElem = 0;
    
        m_P0_stack.pop_back(); // remove the exception element
        pElem = m_P0_stack.back();  // look at next element.
        if(pElem->getP0Type() == P0_CTXT)
        {
            pCtxtElem = dynamic_cast<TrcStackElemCtxt *>(pElem);
            m_P0_stack.pop_back(); // remove the context element
            pElem = m_P0_stack.back();  // next one should be an address element
        }
   
        if(pElem->getP0Type() != P0_ADDR)
        {
            // no following address element - indicate processing error.
            resp = OCSD_RESP_FATAL_INVALID_DATA;
            LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_BAD_PACKET_SEQ,pExceptElem->getRootIndex(),m_CSID,"Address missing in exception packet."));  
        }
        else
        {
            // extract address
            pAddressElem = static_cast<TrcStackElemAddr *>(pElem);

            m_excep_addr = pAddressElem->getAddr();                

            // if we have context, get that.
            if(pCtxtElem)
                 updateContext(pCtxtElem);

            // record the exception number
            m_output_elem.exception_number = pExceptElem->getExcepNum();

            // see if there is an implied P0 element on the exception.
            excep_implied_P0 = pExceptElem->getPrevSame();

            // save the trace index.
            m_excep_index = pExceptElem->getRootIndex();

            // figure out next move
            if(m_excep_addr.val == m_instr_info.instr_addr)
                m_excep_proc = EXCEP_EXCEP;
            else
                m_excep_proc = EXCEP_RANGE;
        }
        m_P0_stack.delete_popped();
    }

    // output a range element
    if(m_excep_proc == EXCEP_RANGE) 
    {
        bool bWPFound = false;
        ocsd_err_t err;

        // last instr_info address is the start address
        m_output_elem.st_addr = m_instr_info.instr_addr;

        // look for either a WP or match to return address.
        err = traceInstrToWP(bWPFound,!excep_implied_P0,m_excep_addr.val);

        if(err != OCSD_OK)
        {
            if(err == OCSD_ERR_UNSUPPORTED_ISA)
            {
                m_need_addr = true;
                m_need_ctxt = true;
                LogError(ocsdError(OCSD_ERR_SEV_WARN,err,m_excep_index,m_CSID,"Warning: unsupported instruction set processing exception packet."));  
            }
            else
            {
                resp = OCSD_RESP_FATAL_INVALID_DATA;
                LogError(ocsdError(OCSD_ERR_SEV_ERROR,err,m_excep_index,m_CSID,"Error processing exception packet."));  
                m_excep_proc = EXCEP_POP;  // nothing more to do, reset to start of exception handling
            }
        }

        if(bWPFound)
        {
            // action according to waypoint type and atom value
            if(excep_implied_P0)
            {
                switch(m_instr_info.type)
                {
                case OCSD_INSTR_BR:
                m_instr_info.instr_addr = m_instr_info.branch_addr;
                break;

                case OCSD_INSTR_BR_INDIRECT:
                m_instr_info.instr_addr = m_excep_addr.val;
                break;
                }
            }
            m_output_elem.setType(OCSD_GEN_TRC_ELEM_INSTR_RANGE);
            m_output_elem.setLastInstrInfo(true,m_instr_info.type, m_instr_info.sub_type);
            m_output_elem.setISA(m_instr_info.isa);
            resp = outputTraceElementIdx(m_excep_index, m_output_elem);
            m_excep_proc = EXCEP_EXCEP;
        }
        else
        {
            // no waypoint - likely inaccessible memory range.
            m_need_addr = true; // need an address update 
            
            if(m_output_elem.st_addr != m_output_elem.en_addr)
            {
                // some trace before we were out of memory access range
                m_output_elem.setType(OCSD_GEN_TRC_ELEM_INSTR_RANGE);
                m_output_elem.setLastInstrInfo(true,m_instr_info.type, m_instr_info.sub_type);
                m_output_elem.setISA(m_instr_info.isa);
                resp = outputTraceElementIdx(m_excep_index,m_output_elem);
            }

            m_excep_proc = m_mem_nacc_pending ? EXCEP_NACC : EXCEP_EXCEP;
        }
    }  
    
    if((m_excep_proc == EXCEP_NACC) && OCSD_DATA_RESP_IS_CONT(resp))
    {
        m_output_elem.setType(OCSD_GEN_TRC_ELEM_ADDR_NACC);
        m_output_elem.st_addr = m_nacc_addr;
        resp = outputTraceElementIdx(m_excep_index,m_output_elem);
        m_excep_proc = EXCEP_EXCEP;
        m_mem_nacc_pending = false;
    }
    
    if((m_excep_proc == EXCEP_EXCEP) && OCSD_DATA_RESP_IS_CONT(resp))
    {
        // output element.
        m_output_elem.setType(OCSD_GEN_TRC_ELEM_EXCEPTION);
        // add end address as preferred return address to end addr in element
        m_output_elem.en_addr = m_excep_addr.val;
        m_output_elem.excep_ret_addr = 1;
        resp = outputTraceElementIdx(m_excep_index,m_output_elem);  
        m_excep_proc = EXCEP_POP;
    }   
    return resp;
}

void TrcPktDecodeEtmV4I::SetInstrInfoInAddrISA(const ocsd_vaddr_t addr_val, const uint8_t isa)
{
    m_instr_info.instr_addr = addr_val;
    if(m_is_64bit)
        m_instr_info.isa = ocsd_isa_aarch64;
    else
        m_instr_info.isa = (isa == 0) ? ocsd_isa_arm : ocsd_isa_thumb2;
}

// trace an instruction range to a waypoint - and set next address to restart from.
ocsd_err_t TrcPktDecodeEtmV4I::traceInstrToWP(bool &bWPFound, const bool traceToAddrNext /*= false*/, const ocsd_vaddr_t nextAddrMatch /*= 0*/)
{
    uint32_t opcode;
    uint32_t bytesReq;
    ocsd_err_t err = OCSD_OK;

    // TBD?: update mem space to allow for EL as well.
    ocsd_mem_space_acc_t mem_space = m_is_secure ? OCSD_MEM_SPACE_S : OCSD_MEM_SPACE_N;

    m_output_elem.st_addr = m_output_elem.en_addr = m_instr_info.instr_addr;

    bWPFound = false;

    while(!bWPFound && !m_mem_nacc_pending)
    {
        // start off by reading next opcode;
        bytesReq = 4;
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

            // either walking to match the next instruction address or a real watchpoint
            if(traceToAddrNext)
                bWPFound = (m_output_elem.en_addr == nextAddrMatch);
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

void TrcPktDecodeEtmV4I::updateContext(TrcStackElemCtxt *pCtxtElem)
{
    etmv4_context_t ctxt = pCtxtElem->getContext();
    // map to output element  and local saved state.
    m_is_64bit = (ctxt.SF != 0);
    m_output_elem.context.bits64 = ctxt.SF;
    m_is_secure = (ctxt.NS == 0);
    m_output_elem.context.security_level = ctxt.NS ? ocsd_sec_nonsecure : ocsd_sec_secure;
    m_output_elem.context.exception_level = (ocsd_ex_level)ctxt.EL;
    m_output_elem.context.el_valid = 1;
    if(ctxt.updated_c)
    {
        m_output_elem.context.ctxt_id_valid = 1;
        m_context_id = m_output_elem.context.context_id = ctxt.ctxtID;
    }
    if(ctxt.updated_v)
    {
        m_output_elem.context.vmid_valid = 1;
        m_vmid_id = m_output_elem.context.vmid = ctxt.VMID;
    }
    m_need_ctxt = false;
}

ocsd_datapath_resp_t TrcPktDecodeEtmV4I::handleBadPacket(const char *reason)
{
    ocsd_datapath_resp_t resp  = OCSD_RESP_CONT;   

    if(getComponentOpMode() && OCSD_OPFLG_PKTDEC_ERROR_BAD_PKTS)
    {
        // error out - stop decoding
        resp = OCSD_RESP_FATAL_INVALID_DATA;
        LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_BAD_DECODE_PKT,reason));
    }
    else
    {
        // switch to unsync - clear decode state
        m_output_elem.setType(OCSD_GEN_TRC_ELEM_NO_SYNC);
        resp = outputTraceElement(m_output_elem);
        resetDecoder();
        m_curr_state = WAIT_SYNC;
    }
    return resp;
}

/* End of File trc_pkt_decode_etmv4i.cpp */
