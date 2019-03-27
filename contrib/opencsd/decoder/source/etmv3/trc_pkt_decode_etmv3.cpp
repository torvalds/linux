/*!
 * \file       trc_pkt_decode_etmv3.cpp
 * \brief      OpenCSD : ETMv3 trace packet decode.
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

#include "opencsd/etmv3/trc_pkt_decode_etmv3.h"

#define DCD_NAME "DCD_ETMV3"

TrcPktDecodeEtmV3::TrcPktDecodeEtmV3() : 
    TrcPktDecodeBase(DCD_NAME)
{
    initDecoder();
}

TrcPktDecodeEtmV3::TrcPktDecodeEtmV3(int instIDNum) :
    TrcPktDecodeBase(DCD_NAME, instIDNum)
{
    initDecoder();
}

TrcPktDecodeEtmV3::~TrcPktDecodeEtmV3()
{
}


/* implementation packet decoding interface */
ocsd_datapath_resp_t TrcPktDecodeEtmV3::processPacket()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    bool bPktDone = false;

    if(!m_config)
        return OCSD_RESP_FATAL_NOT_INIT;

    // iterate round the state machine, waiting for sync, then decoding packets.
    while(!bPktDone)
    {
        switch(m_curr_state)
        {
        case NO_SYNC:
            // output the initial not synced packet to the sink
            resp = sendUnsyncPacket();  
            m_curr_state = WAIT_ASYNC;  // immediate wait for ASync and actually check out the packet
            break;

        case WAIT_ASYNC:
            // if async, wait for ISync, but this packet done.
            if(m_curr_packet_in->getType() == ETM3_PKT_A_SYNC)
                m_curr_state = WAIT_ISYNC;
            bPktDone = true;
            break;

        case WAIT_ISYNC:
            m_bWaitISync = true;    // we are waiting for ISync
            if((m_curr_packet_in->getType() == ETM3_PKT_I_SYNC) || 
                (m_curr_packet_in->getType() == ETM3_PKT_I_SYNC_CYCLE))
            {
                // process the ISync immediately as the first ISync seen.
                resp = processISync((m_curr_packet_in->getType() == ETM3_PKT_I_SYNC_CYCLE),true);
                m_curr_state = SEND_PKTS;
                m_bWaitISync = false;
            }
            // something like TS, CC, PHDR+CC, which after ASYNC may be valid prior to ISync
            else if(preISyncValid(m_curr_packet_in->getType()))
            {
                // decode anything that might be valid - send will be set automatically 
                resp = decodePacket(bPktDone);
            }
            else
                bPktDone = true; 
            break;

        case DECODE_PKTS:
            resp = decodePacket(bPktDone);
            break;

        case SEND_PKTS:
            resp = m_outputElemList.sendElements();
            if(OCSD_DATA_RESP_IS_CONT(resp))
                m_curr_state = m_bWaitISync ? WAIT_ISYNC : DECODE_PKTS;
            bPktDone = true;
            break;  

        default:
            bPktDone = true;
            LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_FAIL,m_index_curr_pkt,"Unknown Decoder State"));
            resetDecoder(); // mark decoder as unsynced - dump any current state.
            resp = OCSD_RESP_FATAL_SYS_ERR;
            break;
        }
    }

    return resp;
}

ocsd_datapath_resp_t TrcPktDecodeEtmV3::onEOT()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    OcsdTraceElement *pElem = 0;
    try {
        pElem = GetNextOpElem(resp);
        pElem->setType(OCSD_GEN_TRC_ELEM_EO_TRACE);
        m_outputElemList.commitAllPendElem();
        m_curr_state = SEND_PKTS;
        resp = m_outputElemList.sendElements();
        if(OCSD_DATA_RESP_IS_CONT(resp))
            m_curr_state = DECODE_PKTS;
    }
    catch(ocsdError &err)
    {
        LogError(err);
        resetDecoder(); // mark decoder as unsynced - dump any current state.
    }    
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodeEtmV3::onReset()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    resetDecoder();
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodeEtmV3::onFlush()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    if(m_curr_state == SEND_PKTS)
    {
        resp = m_outputElemList.sendElements();
        if(OCSD_DATA_RESP_IS_CONT(resp))
            m_curr_state = m_bWaitISync ? WAIT_ISYNC : DECODE_PKTS;
    }
    return resp;
}

ocsd_err_t TrcPktDecodeEtmV3::onProtocolConfig()
{
    ocsd_err_t err = OCSD_OK;
    if(m_config)
    {
        // set some static config elements
        m_CSID = m_config->getTraceID();

        // check config compatible with current decoder support level.
        // at present no data trace;
        if(m_config->GetTraceMode() != EtmV3Config::TM_INSTR_ONLY)
        {
            err = OCSD_ERR_HW_CFG_UNSUPP;
            LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_HW_CFG_UNSUPP,"ETMv3 trace decoder : data trace decode not yet supported"));
        }

        // need to set up core profile info in follower
        ocsd_arch_profile_t arch_profile;
        arch_profile.arch = m_config->getArchVersion();
        arch_profile.profile = m_config->getCoreProfile();
        m_code_follower.setArchProfile(arch_profile);
        m_code_follower.setMemSpaceCSID(m_CSID);
        m_outputElemList.initCSID(m_CSID);
    }
    else
        err = OCSD_ERR_NOT_INIT;
    return err;
}

/* local decode methods */

// initialise on creation
void TrcPktDecodeEtmV3::initDecoder()
{
    m_CSID = 0;
    resetDecoder();
    m_code_follower.initInterfaces(getMemoryAccessAttachPt(),getInstrDecodeAttachPt());
    m_outputElemList.initSendIf(getTraceElemOutAttachPt());
}

// reset for first use / re-use.
void TrcPktDecodeEtmV3::resetDecoder()
{
    m_curr_state = NO_SYNC; // mark as not synced
    m_bNeedAddr = true;
    m_bSentUnknown = false;
    m_bWaitISync = false;
    m_outputElemList.reset();
}

OcsdTraceElement *TrcPktDecodeEtmV3::GetNextOpElem(ocsd_datapath_resp_t &resp)
{
    OcsdTraceElement *pElem = m_outputElemList.getNextElem(m_index_curr_pkt);
    if(pElem == 0)
    {
        resp = OCSD_RESP_FATAL_NOT_INIT;
        throw ocsdError(OCSD_ERR_SEV_ERROR, OCSD_ERR_MEM,m_index_curr_pkt,m_CSID,"Memory Allocation Error - fatal");
    }
    return pElem;
}

bool TrcPktDecodeEtmV3::preISyncValid(ocsd_etmv3_pkt_type pkt_type)
{
    bool bValid = false;
    // its a timestamp
    if((pkt_type == ETM3_PKT_TIMESTAMP) || 
        // or we are cycleacc and its a packet that can have CC in it
        (m_config->isCycleAcc() && ((pkt_type == ETM3_PKT_CYCLE_COUNT) || (pkt_type == ETM3_PKT_P_HDR)))
        )
        bValid = true;
    return bValid;
}

// simple packet transforms handled here, more complex processing passed on to specific routines.
ocsd_datapath_resp_t TrcPktDecodeEtmV3::decodePacket(bool &pktDone)
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    bool bISyncHasCC = false;
    OcsdTraceElement *pElem = 0;
    pktDone = false;

    // there may be pended packets that can now be committed.
    // only the branch address with exception and cancel element can cancel
    // if not one of those, commit immediately, otherwise defer to branch address handler.
    if(m_curr_packet_in->getType() != ETM3_PKT_BRANCH_ADDRESS)
        m_outputElemList.commitAllPendElem();

    try {

        switch(m_curr_packet_in->getType())
        {

        case ETM3_PKT_NOTSYNC:
            // mark as not synced - must have lost sync in the packet processor somehow
            throw ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_BAD_PACKET_SEQ,m_index_curr_pkt,m_CSID,"Trace Packet Synchronisation Lost");
            break;

            // no action for these packets - ignore and continue
        case ETM3_PKT_INCOMPLETE_EOT:
        case ETM3_PKT_A_SYNC:
        case ETM3_PKT_IGNORE:
            break;

    // markers for valid packets
        case ETM3_PKT_CYCLE_COUNT:
            pElem = GetNextOpElem(resp);
            pElem->setType(OCSD_GEN_TRC_ELEM_CYCLE_COUNT);
            pElem->setCycleCount(m_curr_packet_in->getCycleCount());
            break;

        case ETM3_PKT_TRIGGER:
            pElem = GetNextOpElem(resp);
            pElem->setType(OCSD_GEN_TRC_ELEM_EVENT);
            pElem->setEvent(EVENT_TRIGGER,0);
            break;

        case ETM3_PKT_BRANCH_ADDRESS:
            resp = processBranchAddr();
            break;

        case ETM3_PKT_I_SYNC_CYCLE:
            bISyncHasCC = true;
        case ETM3_PKT_I_SYNC:
            resp = processISync(bISyncHasCC);
            break;

        case ETM3_PKT_P_HDR:
            resp = processPHdr();
            break;

        case ETM3_PKT_CONTEXT_ID:
            pElem = GetNextOpElem(resp);
            pElem->setType(OCSD_GEN_TRC_ELEM_PE_CONTEXT);
            m_PeContext.setCtxtID(m_curr_packet_in->getCtxtID());
            pElem->setContext(m_PeContext);
            break;

        case ETM3_PKT_VMID:
            pElem = GetNextOpElem(resp);
            pElem->setType(OCSD_GEN_TRC_ELEM_PE_CONTEXT);
            m_PeContext.setVMID(m_curr_packet_in->getVMID());
            pElem->setContext(m_PeContext);
            break;

        case ETM3_PKT_EXCEPTION_ENTRY:
            pElem = GetNextOpElem(resp);
            pElem->setType(OCSD_GEN_TRC_ELEM_EXCEPTION);
            pElem->setExcepMarker(); // exception entries are always v7M data markers in ETMv3 trace.
            break;

        case ETM3_PKT_EXCEPTION_EXIT:
            pElem = GetNextOpElem(resp);
            pElem->setType(OCSD_GEN_TRC_ELEM_EXCEPTION_RET);
            pendExceptionReturn();
            break;
        
        case ETM3_PKT_TIMESTAMP:
            pElem = GetNextOpElem(resp);
            pElem->setType(OCSD_GEN_TRC_ELEM_TIMESTAMP);
            pElem->setTS(m_curr_packet_in->getTS());
            break;

            // data packets - data trace not supported at present
        case ETM3_PKT_STORE_FAIL:
        case ETM3_PKT_OOO_DATA:
        case ETM3_PKT_OOO_ADDR_PLC:
        case ETM3_PKT_NORM_DATA:
        case ETM3_PKT_DATA_SUPPRESSED:
        case ETM3_PKT_VAL_NOT_TRACED:
        case ETM3_PKT_BAD_TRACEMODE:
            resp = OCSD_RESP_FATAL_INVALID_DATA;        
            throw ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_HW_CFG_UNSUPP,m_index_curr_pkt,m_CSID,"Invalid packet type : Data Tracing decode not supported.");
            break;

    // packet errors 
        case ETM3_PKT_BAD_SEQUENCE:
            resp = OCSD_RESP_FATAL_INVALID_DATA;
            throw ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_BAD_PACKET_SEQ,m_index_curr_pkt,m_CSID,"Bad Packet sequence.");
            break;

        default:
        case ETM3_PKT_RESERVED:
            resp = OCSD_RESP_FATAL_INVALID_DATA;
            throw ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_BAD_PACKET_SEQ,m_index_curr_pkt,m_CSID,"Reserved or unknown packet ID.");
            break;
        }    
        m_curr_state = m_outputElemList.elemToSend() ? SEND_PKTS : DECODE_PKTS;
        pktDone = !m_outputElemList.elemToSend();
    }
    catch(ocsdError &err)
    {
        LogError(err);
        resetDecoder(); // mark decoder as unsynced - dump any current state.
        pktDone = true;
    }
    catch(...)
    {
        LogError(ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_FAIL,m_index_curr_pkt,m_CSID,"Bad Packet sequence."));
        resp = OCSD_RESP_FATAL_SYS_ERR;
        resetDecoder(); // mark decoder as unsynced - dump any current state.
        pktDone = true;
    }
    return resp;
}
   
ocsd_datapath_resp_t TrcPktDecodeEtmV3::sendUnsyncPacket()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    OcsdTraceElement *pElem = 0;
    try {
        pElem = GetNextOpElem(resp);
        pElem->setType(OCSD_GEN_TRC_ELEM_NO_SYNC);
        resp = m_outputElemList.sendElements();
    }
    catch(ocsdError &err)
    {
        LogError(err);
        resetDecoder(); // mark decoder as unsynced - dump any current state.
    }
    return resp;
}

void TrcPktDecodeEtmV3::setNeedAddr(bool bNeedAddr)
{
    m_bNeedAddr = bNeedAddr;
    m_bSentUnknown = false;    
}

ocsd_datapath_resp_t TrcPktDecodeEtmV3::processISync(const bool withCC, const bool firstSync /* = false */)
{
    // map ISync reason to generic reason codes.
    static trace_on_reason_t on_map[] = { TRACE_ON_NORMAL, TRACE_ON_NORMAL,
        TRACE_ON_OVERFLOW, TRACE_ON_EX_DEBUG };

    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    bool ctxtUpdate = m_curr_packet_in->isCtxtUpdated();   
    OcsdTraceElement *pElem = 0;

    try {

        pElem = GetNextOpElem(resp);

        if(firstSync || (m_curr_packet_in->getISyncReason() != iSync_Periodic))
        {
            pElem->setType(OCSD_GEN_TRC_ELEM_TRACE_ON);
            pElem->setTraceOnReason(on_map[(int)m_curr_packet_in->getISyncReason()]);
            pElem =  GetNextOpElem(resp);           
        }

        // look for context changes....
        if(ctxtUpdate || firstSync)
        {
            // if not first time out, read existing context in output element,
            // otherwise we are setting it new.
            if(firstSync)  
                m_PeContext.resetCtxt();

            if(m_curr_packet_in->isCtxtIDUpdated())
                m_PeContext.setCtxtID(m_curr_packet_in->getCtxtID());
            if(m_curr_packet_in->isVMIDUpdated())
                m_PeContext.setVMID(m_curr_packet_in->getVMID());
            if(m_curr_packet_in->isCtxtFlagsUpdated())
            {
                m_PeContext.setEL(m_curr_packet_in->isHyp() ? ocsd_EL2 : ocsd_EL_unknown);
                m_PeContext.setSecLevel(m_curr_packet_in->isNS() ? ocsd_sec_nonsecure : ocsd_sec_secure);
            }

            // prepare the context packet
            pElem->setType(OCSD_GEN_TRC_ELEM_PE_CONTEXT);
            pElem->setContext(m_PeContext);
            pElem->setISA(m_curr_packet_in->ISA());

            // with cycle count...
            if(m_curr_packet_in->getISyncHasCC())
                pElem->setCycleCount(m_curr_packet_in->getCycleCount());

        }

        // set ISync address - if it is a valid I address
        if(!m_curr_packet_in->getISyncNoAddr())
        {
            if(m_curr_packet_in->getISyncIsLSiPAddr())
            {
                // TBD: handle extra data processing instruction for data trace
                // need to output E atom relating to the data instruction
                // rare - on start-up case.

                // main instruction address saved in data address for this packet type.
                m_IAddr = m_curr_packet_in->getDataAddr();
            }
            else
            {
                m_IAddr = m_curr_packet_in->getAddr();
            }
            setNeedAddr(false);    // ready to process atoms.
        }
        m_curr_state = m_outputElemList.elemToSend() ? SEND_PKTS : DECODE_PKTS;
    }
    catch(ocsdError &err)
    {
        LogError(err);
        resetDecoder(); // mark decoder as unsynced - dump any current state.
    }
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodeEtmV3::processBranchAddr()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    OcsdTraceElement *pElem = 0;
    bool bUpdatePEContext = false;

    // might need to cancel something ... if the last output was an instruction range or excep return
    if(m_curr_packet_in->isExcepCancel())
        m_outputElemList.cancelPendElem();
    else         
        m_outputElemList.commitAllPendElem(); // otherwise commit any pending elements.

    // record the address
    m_IAddr = m_curr_packet_in->getAddr();
    setNeedAddr(false);    // no longer need an address.
   
    // exception packet - may need additional output
    if(m_curr_packet_in->isExcepPkt())
    {
        // exeception packet may have exception, context change, or both.
        // check for context change
        if(m_curr_packet_in->isCtxtUpdated())
        {
            
            ocsd_sec_level sec = m_curr_packet_in->isNS() ? ocsd_sec_nonsecure : ocsd_sec_secure;
            if(sec != m_PeContext.getSecLevel())
            {
                m_PeContext.setSecLevel(sec);
                bUpdatePEContext = true;
            }
            ocsd_ex_level pkt_el = m_curr_packet_in->isHyp() ?  ocsd_EL2 : ocsd_EL_unknown;
            if(pkt_el != m_PeContext.getEL())
            {
                m_PeContext.setEL(pkt_el);
                bUpdatePEContext = true;
            }
        }

        // now decide if we need to send any packets out.
        try {

            if(bUpdatePEContext)
            {
                pElem = GetNextOpElem(resp);
                pElem->setType(OCSD_GEN_TRC_ELEM_PE_CONTEXT);
                pElem->setContext(m_PeContext);
            }

            // check for exception
            if(m_curr_packet_in->excepNum() != 0)
            {
                pElem = GetNextOpElem(resp);
                pElem->setType(OCSD_GEN_TRC_ELEM_EXCEPTION);
                pElem->setExceptionNum(m_curr_packet_in->excepNum());                        
            }

            // finally - do we have anything to send yet?
            m_curr_state = m_outputElemList.elemToSend() ? SEND_PKTS : DECODE_PKTS;
        }
        catch(ocsdError &err)
        {
            LogError(err);
            resetDecoder(); // mark decoder as unsynced - dump any current state.
        }
    }       
    return resp;
}


ocsd_datapath_resp_t TrcPktDecodeEtmV3::processPHdr()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    OcsdTraceElement *pElem = 0;
    ocsd_isa isa;
    Etmv3Atoms atoms(m_config->isCycleAcc());

    atoms.initAtomPkt(m_curr_packet_in,m_index_curr_pkt);
    isa = m_curr_packet_in->ISA();
    m_code_follower.setMemSpaceAccess((m_PeContext.getSecLevel() ==  ocsd_sec_secure) ? OCSD_MEM_SPACE_S : OCSD_MEM_SPACE_N);

    try
    {
        do 
        {
            // if we do not have a valid address then send any cycle count elements 
            // and stop processing
            if(m_bNeedAddr)
            {
                // output unknown address packet or a cycle count packet
                if(!m_bSentUnknown || m_config->isCycleAcc())
                {
                    pElem = GetNextOpElem(resp);
                    if(m_bSentUnknown || !atoms.numAtoms())
                        pElem->setType(OCSD_GEN_TRC_ELEM_CYCLE_COUNT);
                    else
                        pElem->setType(OCSD_GEN_TRC_ELEM_ADDR_UNKNOWN);
                    if(m_config->isCycleAcc())
                        pElem->setCycleCount(atoms.getRemainCC());
                    m_bSentUnknown = true;
                }
                atoms.clearAll();   // skip remaining atoms
            }
            else    // have an address, can process atoms
            {
                pElem = GetNextOpElem(resp);
                pElem->setType(OCSD_GEN_TRC_ELEM_INSTR_RANGE);
    
                // cycle accurate may have a cycle count to use
                if(m_config->isCycleAcc())
                {
                    // note: it is possible to have a CC only atom packet.
                    if(!atoms.numAtoms())   // override type if CC only
                         pElem->setType(OCSD_GEN_TRC_ELEM_CYCLE_COUNT);
                    // set cycle count
                    pElem->setCycleCount(atoms.getAtomCC());
                }

                // now process the atom 
                if(atoms.numAtoms())
                {
                    m_code_follower.setISA(isa);
                    m_code_follower.followSingleAtom(m_IAddr,atoms.getCurrAtomVal());

                    // valid code range
                    if(m_code_follower.hasRange())
                    {
                        pElem->setAddrRange(m_IAddr,m_code_follower.getRangeEn());
                        pElem->setLastInstrInfo(atoms.getCurrAtomVal() == ATOM_E, 
                                    m_code_follower.getInstrType(),
                                    m_code_follower.getInstrSubType());
                        pElem->setISA(isa);
                        if(m_code_follower.hasNextAddr())
                            m_IAddr = m_code_follower.getNextAddr();
                        else
                            setNeedAddr(true);
                    }

                    // next address has new ISA?
                    if(m_code_follower.ISAChanged())
                        isa = m_code_follower.nextISA();
                    
                    // there is a nacc
                    if(m_code_follower.isNacc())
                    {
                        if(m_code_follower.hasRange())
                        {
                            pElem = GetNextOpElem(resp);
                            pElem->setType(OCSD_GEN_TRC_ELEM_ADDR_NACC);
                        }
                        else
                            pElem->updateType(OCSD_GEN_TRC_ELEM_ADDR_NACC);
                        pElem->setAddrStart(m_code_follower.getNaccAddr());
                        setNeedAddr(true);
                        m_code_follower.clearNacc(); // we have generated some code for the nacc.
                    }
                }

                atoms.clearAtom();  // next atom
            }
        }
        while(atoms.numAtoms());

        // is tha last element an atom?
        int numElem = m_outputElemList.getNumElem();
        if(numElem >= 1)
        {
            // if the last thing is an instruction range, pend it - could be cancelled later.
            if(m_outputElemList.getElemType(numElem-1) == OCSD_GEN_TRC_ELEM_INSTR_RANGE)
                m_outputElemList.pendLastNElem(1);
        }

        // finally - do we have anything to send yet?
        m_curr_state = m_outputElemList.elemToSend() ? SEND_PKTS : DECODE_PKTS;
    }
    catch(ocsdError &err)
    {
        LogError(err);
        resetDecoder(); // mark decoder as unsynced - dump any current state.
    }
    return resp;
}

// if v7M -> pend only ERET, if V7A/R pend ERET and prev instr.
void TrcPktDecodeEtmV3::pendExceptionReturn()
{
    int pendElem = 1;
    if(m_config->getCoreProfile() != profile_CortexM)
    {
        int nElem = m_outputElemList.getNumElem();
        if(nElem > 1)
        {
           if(m_outputElemList.getElemType(nElem - 2) == OCSD_GEN_TRC_ELEM_INSTR_RANGE)
               pendElem = 2;    // need to pend instr+eret for A/R
        }
    }
    m_outputElemList.pendLastNElem(pendElem);
}

/* End of File trc_pkt_decode_etmv3.cpp */
