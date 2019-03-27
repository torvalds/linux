/*
 * \file       trc_pkt_proc_etmv4i_impl.cpp
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

#include "trc_pkt_proc_etmv4i_impl.h"

EtmV4IPktProcImpl::EtmV4IPktProcImpl() :
    m_isInit(false),
    m_interface(0),
    m_first_trace_info(false)
{
    BuildIPacketTable();
}

EtmV4IPktProcImpl::~EtmV4IPktProcImpl()
{
}

void EtmV4IPktProcImpl::Initialise(TrcPktProcEtmV4I *p_interface)
{
     if(p_interface)
     {
         m_interface = p_interface;
         m_isInit = true;
     }
     InitProcessorState();
}

ocsd_err_t EtmV4IPktProcImpl::Configure(const EtmV4Config *p_config)
{
    ocsd_err_t err = OCSD_OK;
    if(p_config != 0)
    {
        m_config = *p_config;
    }
    else
    {
        err = OCSD_ERR_INVALID_PARAM_VAL;
        if(m_isInit)
            m_interface->LogError(ocsdError(OCSD_ERR_SEV_ERROR,err));
    }
    return err;
}

ocsd_datapath_resp_t EtmV4IPktProcImpl::processData(  const ocsd_trc_index_t index,
                                    const uint32_t dataBlockSize,
                                    const uint8_t *pDataBlock,
                                    uint32_t *numBytesProcessed)
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    m_blockBytesProcessed = 0;
    m_blockIndex = index;
    uint8_t currByte;
    while(  ( (m_blockBytesProcessed < dataBlockSize) || 
              ((m_blockBytesProcessed == dataBlockSize) && (m_process_state == SEND_PKT)) ) && 
            OCSD_DATA_RESP_IS_CONT(resp))
    {
        currByte = pDataBlock[m_blockBytesProcessed];
        try 
        {
            switch(m_process_state)
            {
            case PROC_HDR:
                m_packet_index = m_blockIndex +  m_blockBytesProcessed;
                if(m_is_sync)
                {
                    m_pIPktFn = m_i_table[currByte].pptkFn;
                    m_curr_packet.type = m_i_table[currByte].pkt_type;
                }
                else
                {
                    // unsynced - process data until we see a sync point
                    m_pIPktFn = &EtmV4IPktProcImpl::iNotSync;
                    m_curr_packet.type = ETM4_PKT_I_NOTSYNC;
                }
                m_process_state = PROC_DATA;

            case PROC_DATA:
                m_currPacketData.push_back(pDataBlock[m_blockBytesProcessed]);
                m_blockBytesProcessed++;
                (this->*m_pIPktFn)();                
                break;

            case SEND_PKT:
                resp =  outputPacket();
                InitPacketState();
                m_process_state = PROC_HDR;
                break;

            case SEND_UNSYNCED:
                resp = outputUnsyncedRawPacket();
                if(m_update_on_unsync_packet_index != 0)
                {
                    m_packet_index = m_update_on_unsync_packet_index;
                    m_update_on_unsync_packet_index = 0;
                }
                m_process_state = PROC_DATA;        // after dumping unsynced data, still in data mode.
                break;
            }
        }
        catch(ocsdError &err)
        {
            m_interface->LogError(err);
            if( (err.getErrorCode() == OCSD_ERR_BAD_PACKET_SEQ) ||
                (err.getErrorCode() == OCSD_ERR_INVALID_PCKT_HDR))
            {
                // send invalid packets up the pipe to let the next stage decide what to do.
                m_process_state = SEND_PKT; 
            }
            else
            {
                // bail out on any other error.
                resp = OCSD_RESP_FATAL_INVALID_DATA;
            }
        }
        catch(...)
        {
            /// vv bad at this point.
            resp = OCSD_RESP_FATAL_SYS_ERR;
            const ocsdError &fatal = ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_FAIL,m_packet_index,m_config.getTraceID(),"Unknown System Error decoding trace.");
            m_interface->LogError(fatal);
        }
    }

    *numBytesProcessed = m_blockBytesProcessed;
    return resp;
}

ocsd_datapath_resp_t EtmV4IPktProcImpl::onEOT()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    // if we have a partial packet then send to attached sinks
    if(m_currPacketData.size() != 0)
    {
        m_curr_packet.updateErrType(ETM4_PKT_I_INCOMPLETE_EOT);
        resp = outputPacket();
        InitPacketState();
    }
    return resp;
}

ocsd_datapath_resp_t EtmV4IPktProcImpl::onReset()
{
    // prepare for new decoding session
    InitProcessorState();
    return OCSD_RESP_CONT;
}

ocsd_datapath_resp_t EtmV4IPktProcImpl::onFlush()
{
    // packet processor never holds on to flushable data (may have partial packet, 
    // but any full packets are immediately sent)
    return OCSD_RESP_CONT;
}

void EtmV4IPktProcImpl::InitPacketState()
{
    m_currPacketData.clear();
    m_curr_packet.initNextPacket(); // clear for next packet.
    m_update_on_unsync_packet_index = 0;
}

void EtmV4IPktProcImpl::InitProcessorState()
{
    InitPacketState();
    m_pIPktFn = &EtmV4IPktProcImpl::iNotSync;
    m_packet_index = 0;
    m_is_sync = false;
    m_first_trace_info = false;
    m_sent_notsync_packet = false;
    m_process_state = PROC_HDR;
    m_curr_packet.initStartState();
}

ocsd_datapath_resp_t EtmV4IPktProcImpl::outputPacket()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    resp = m_interface->outputOnAllInterfaces(m_packet_index,&m_curr_packet,&m_curr_packet.type,m_currPacketData);
    return resp;
}

ocsd_datapath_resp_t EtmV4IPktProcImpl::outputUnsyncedRawPacket()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    

    m_interface->outputRawPacketToMonitor(m_packet_index,&m_curr_packet,m_dump_unsynced_bytes,&m_currPacketData[0]);
        
    if(!m_sent_notsync_packet)
    {        
        resp = m_interface->outputDecodedPacket(m_packet_index,&m_curr_packet);
        m_sent_notsync_packet = true;
    }
    
    if(m_currPacketData.size() <= m_dump_unsynced_bytes)
        m_currPacketData.clear();
    else
        m_currPacketData.erase(m_currPacketData.begin(),m_currPacketData.begin()+m_dump_unsynced_bytes);

    return resp;
}

void EtmV4IPktProcImpl::iNotSync()
{
    uint8_t lastByte = m_currPacketData.back(); // peek at the byte being processed...

    // is it an extension byte? 
    if(lastByte == 0x00) // TBD : add check for forced sync in here?
    {
        if(m_currPacketData.size() > 1)
        {
            m_dump_unsynced_bytes = m_currPacketData.size() - 1;
            m_process_state = SEND_UNSYNCED;
            // outputting some data then update packet index after so output indexes accurate
            m_update_on_unsync_packet_index = m_blockIndex + m_blockBytesProcessed - 1;
        }
        else
            m_packet_index = m_blockIndex + m_blockBytesProcessed - 1;  // set it up now otherwise.

        m_pIPktFn = m_i_table[lastByte].pptkFn; 
    }
    else if(m_currPacketData.size() >= 8)
    {
        m_dump_unsynced_bytes = m_currPacketData.size();
        m_process_state = SEND_UNSYNCED;
        // outputting some data then update packet index after so output indexes accurate
        m_update_on_unsync_packet_index = m_blockIndex + m_blockBytesProcessed;
    }
}

void EtmV4IPktProcImpl::iPktNoPayload()
{
    // some expansion may be required...
    uint8_t lastByte = m_currPacketData.back();
    switch(m_curr_packet.type)
    {
    case ETM4_PKT_I_ADDR_MATCH:
        m_curr_packet.setAddressExactMatch(lastByte & 0x3);
        break;

    case ETM4_PKT_I_EVENT:
        m_curr_packet.setEvent(lastByte & 0xF);
        break;

    case ETM4_PKT_I_NUM_DS_MKR:
    case ETM4_PKT_I_UNNUM_DS_MKR:
        m_curr_packet.setDataSyncMarker(lastByte & 0x7);
        break;

    // these just need the packet type - no processing required.
    case ETM4_PKT_I_COND_FLUSH:
    case ETM4_PKT_I_EXCEPT_RTN:
    case ETM4_PKT_I_TRACE_ON:
    default: break;
    }
    m_process_state = SEND_PKT; // now just send it....
}

void EtmV4IPktProcImpl::iPktReserved()
{
    m_curr_packet.updateErrType(ETM4_PKT_I_RESERVED);   // swap type for err type
    throw ocsdError(OCSD_ERR_SEV_ERROR, OCSD_ERR_INVALID_PCKT_HDR,m_packet_index,m_config.getTraceID());
}

void EtmV4IPktProcImpl::iPktExtension()
{
    uint8_t lastByte = m_currPacketData.back();
    if(m_currPacketData.size() == 2)
    {
        // not sync and not next by 0x00 - not sync sequence
        if(!m_is_sync && (lastByte != 0x00))
        {
            m_pIPktFn = &EtmV4IPktProcImpl::iNotSync;
            m_curr_packet.type = ETM4_PKT_I_NOTSYNC;
            return;
        }

        switch(lastByte)
        {
        case 0x03: // discard packet.
            m_curr_packet.type = ETM4_PKT_I_DISCARD;
            m_process_state = SEND_PKT;
            break;

        case 0x05:
            m_curr_packet.type = ETM4_PKT_I_OVERFLOW;
            m_process_state = SEND_PKT;
            break;

        case 0x00:
            m_curr_packet.type = ETM4_PKT_I_ASYNC;
            m_pIPktFn = &EtmV4IPktProcImpl::iPktASync;  // handle subsequent bytes as async
            break;

        default:
            m_curr_packet.err_type = m_curr_packet.type;
            m_curr_packet.type = ETM4_PKT_I_BAD_SEQUENCE;
            m_process_state = SEND_PKT;
            break;
        }
    }
}

void EtmV4IPktProcImpl::iPktASync()
{
    uint8_t lastByte = m_currPacketData.back();
    if(lastByte != 0x00)
    {
        // not sync and not next by 0x00 - not sync sequence if < 12
        if(!m_is_sync && m_currPacketData.size() != 12)
        {
            m_pIPktFn = &EtmV4IPktProcImpl::iNotSync;
            m_curr_packet.type = ETM4_PKT_I_NOTSYNC;
            return;
        }

        // 12 bytes and not valid sync sequence - not possible even if not synced
        m_process_state = SEND_PKT;
        if((m_currPacketData.size() != 12) || (lastByte != 0x80))
        {
            m_curr_packet.type = ETM4_PKT_I_BAD_SEQUENCE;
            m_curr_packet.err_type = ETM4_PKT_I_ASYNC;
        }
        else
             m_is_sync = true;  // found a sync packet, mark decoder as synchronised.
    }
    else if(m_currPacketData.size() == 12)
    {
        if(!m_is_sync)
        {
            // if we are not yet synced then ignore extra leading 0x00.
            m_dump_unsynced_bytes = 1;
            m_process_state = SEND_UNSYNCED;
        }
        else
        {
            // bad periodic ASYNC sequence.
            m_curr_packet.type = ETM4_PKT_I_BAD_SEQUENCE;
            m_curr_packet.err_type = ETM4_PKT_I_ASYNC;
            m_process_state = SEND_PKT;
        }
    }
}

void EtmV4IPktProcImpl::iPktTraceInfo()
{
    uint8_t lastByte = m_currPacketData.back();
    if(m_currPacketData.size() == 1)    // header
    {
        //clear flags
        m_tinfo_sections.sectFlags = 0; // mark all sections as incomplete.
        m_tinfo_sections.ctrlBytes = 1; // assume only a single control section byte for now
        
    }
    else if(m_currPacketData.size() == 2) // first payload control byte
    {
        // figure out which sections are absent and set to true - opposite of bitfeild in byte;
        m_tinfo_sections.sectFlags = (~lastByte) & TINFO_ALL_SECT;

        // see if there is an extended control section, otherwise this byte is it.
        if((lastByte & 0x80) == 0x0)
            m_tinfo_sections.sectFlags |= TINFO_CTRL;
 
    }
    else
    {
        if(!(m_tinfo_sections.sectFlags & TINFO_CTRL))
        {
            m_tinfo_sections.sectFlags |= (lastByte & 0x80) ? 0 : TINFO_CTRL;
            m_tinfo_sections.ctrlBytes++;
        }
        else if(!(m_tinfo_sections.sectFlags & TINFO_INFO_SECT))
            m_tinfo_sections.sectFlags |= (lastByte & 0x80) ? 0 : TINFO_INFO_SECT;
        else if(!(m_tinfo_sections.sectFlags & TINFO_KEY_SECT))
            m_tinfo_sections.sectFlags |= (lastByte & 0x80) ? 0 : TINFO_KEY_SECT;
        else if(!(m_tinfo_sections.sectFlags & TINFO_SPEC_SECT))
            m_tinfo_sections.sectFlags |= (lastByte & 0x80) ? 0 : TINFO_SPEC_SECT;
        else if(!(m_tinfo_sections.sectFlags & TINFO_CYCT_SECT))
            m_tinfo_sections.sectFlags |= (lastByte & 0x80) ? 0 : TINFO_CYCT_SECT;
    }

    // all sections accounted for?
    if(m_tinfo_sections.sectFlags == TINFO_ALL)
    {
        // index of first section is number of payload control bytes + 1 for header byte
        unsigned idx = m_tinfo_sections.ctrlBytes + 1;
        uint32_t fieldVal = 0;
        uint8_t presSect = m_currPacketData[1] & TINFO_ALL_SECT;  // first payload control byte

        m_curr_packet.clearTraceInfo();

        if((presSect & TINFO_INFO_SECT) && (idx < m_currPacketData.size()))
        {
            idx += extractContField(m_currPacketData,idx,fieldVal);
            m_curr_packet.setTraceInfo(fieldVal);
        }
        if((presSect & TINFO_KEY_SECT) && (idx < m_currPacketData.size()))
        {
            idx += extractContField(m_currPacketData,idx,fieldVal);
            m_curr_packet.setTraceInfoKey(fieldVal);
        }
        if((presSect & TINFO_SPEC_SECT) && (idx < m_currPacketData.size()))
        {
            idx += extractContField(m_currPacketData,idx,fieldVal);
            m_curr_packet.setTraceInfoSpec(fieldVal);
        }
        if((presSect & TINFO_CYCT_SECT) && (idx < m_currPacketData.size()))
        {
            idx += extractContField(m_currPacketData,idx,fieldVal);
            m_curr_packet.setTraceInfoCyct(fieldVal);
        }
        m_process_state = SEND_PKT;
        m_first_trace_info = true;
    }

}

void EtmV4IPktProcImpl::iPktTimestamp()
{
    // save the latest byte
    uint8_t lastByte = m_currPacketData.back();

    // process the header byte
    if(m_currPacketData.size() == 1)
    {
        m_ccount_done = (bool)((lastByte & 0x1) == 0); // 0 = not present
        m_ts_done = false;
        m_ts_bytes = 0;
    }
    else
    {        
        if(!m_ts_done)
        {
            m_ts_bytes++;
            m_ts_done = (m_ts_bytes == 9) || ((lastByte & 0x80) == 0);
        }
        else if(!m_ccount_done)
        {
            m_ccount_done = (bool)((lastByte & 0x80) == 0);
            // TBD: check for oorange ccount - bad packet.
        }
    }

    if(m_ts_done && m_ccount_done)
    {        
        int idx = 1;
        uint64_t tsVal;
        int ts_bytes = extractContField64(m_currPacketData, idx, tsVal);
        int ts_bits = ts_bytes < 7 ? ts_bytes * 7 : 64;

        if(!m_curr_packet.pkt_valid.bits.ts_valid && m_first_trace_info)
            ts_bits = 64;   // after trace info, missing bits are all 0.

        m_curr_packet.setTS(tsVal,(uint8_t)ts_bits);

        if((m_currPacketData[0] & 0x1) == 0x1)
        {
            uint32_t countVal, countMask;
            
            idx += ts_bytes;           
            extractContField(m_currPacketData, idx, countVal, 3);    // only 3 possible count bytes.
            countMask = (((uint32_t)1UL << m_config.ccSize()) - 1); // mask of the CC size
            countVal &= countMask;
            m_curr_packet.setCycleCount(countVal);
        }

        m_process_state = SEND_PKT;
    }
}

void EtmV4IPktProcImpl::iPktException()
{
    uint8_t lastByte = m_currPacketData.back();

    switch(m_currPacketData.size())
    {
    case 1: m_excep_size = 3; break;
    case 2: if((lastByte & 0x80) == 0x00)
                m_excep_size = 2; 
            break;
    }

    if(m_currPacketData.size() ==  (unsigned)m_excep_size)
    {
        uint16_t excep_type =  (m_currPacketData[1] >> 1) & 0x1F;
        uint8_t addr_interp = (m_currPacketData[1] & 0x40) >> 5 | (m_currPacketData[1] & 0x1);
        uint8_t m_fault_pending = 0;        
        uint8_t m_type = (m_config.coreProfile() == profile_CortexM) ? 1 : 0;

        // extended exception packet (probably M class);
        if(m_currPacketData[1] & 0x80)
        {
            excep_type |= ((uint16_t)m_currPacketData[2] & 0x1F) << 5;
            m_fault_pending = (m_currPacketData[2] >> 5)  & 0x1;
        }
        m_curr_packet.setExceptionInfo(excep_type,addr_interp,m_fault_pending, m_type);
        m_process_state = SEND_PKT;

        // allow the standard address packet handlers to process the address packet field for the exception.
    }
}

void EtmV4IPktProcImpl::iPktCycleCntF123()
{
    ocsd_etmv4_i_pkt_type format = m_curr_packet.type;

    uint8_t lastByte = m_currPacketData.back();
    if( m_currPacketData.size() == 1)
    {
        m_count_done = m_commit_done = false; 
        m_has_count = true;

        if(format == ETM4_PKT_I_CCNT_F3)
        {
            // no commit section for TRCIDR0.COMMOPT == 1
            if(!m_config.commitOpt1())
            {
                m_curr_packet.setCommitElements(((lastByte >> 2) & 0x3) + 1);
            }
            // TBD: warning of non-valid CC threshold here?
            m_curr_packet.setCycleCount(m_curr_packet.getCCThreshold() + (lastByte & 0x3));
            m_process_state = SEND_PKT;
        }
        else if(format == ETM4_PKT_I_CCNT_F1) 
        {
            if((lastByte & 0x1) == 0x1)
            {
                m_has_count = false;
                m_count_done = true;
            }

            // no commit section for TRCIDR0.COMMOPT == 1
            if(m_config.commitOpt1())
                m_commit_done = true;
        }
    }
    else if((format == ETM4_PKT_I_CCNT_F2) && ( m_currPacketData.size() == 2))
    {
        int commit_offset = ((lastByte & 0x1) == 0x1) ? ((int)m_config.MaxSpecDepth() - 15) : 1;
        int commit_elements = ((lastByte >> 4) & 0xF);
        commit_elements += commit_offset;

        // TBD: warning if commit elements < 0?

        m_curr_packet.setCycleCount(m_curr_packet.getCCThreshold() + (lastByte & 0xF));
        m_curr_packet.setCommitElements(commit_elements);
        m_process_state = SEND_PKT;
    }
    else
    {
        // F1 and size 2 or more
        if(!m_commit_done)
            m_commit_done = ((lastByte & 0x80) == 0x00);
        else if(!m_count_done)
            m_count_done = ((lastByte & 0x80) == 0x00);
    }

    if((format == ETM4_PKT_I_CCNT_F1) && m_commit_done && m_count_done)
    {        
        int idx = 1; // index into buffer for payload data.
        uint32_t field_value = 0;
        // no commit section for TRCIDR0.COMMOPT == 1
        if(!m_config.commitOpt1())
        {
            idx += extractContField(m_currPacketData,idx,field_value);
            m_curr_packet.setCommitElements(field_value);
        }
		if (m_has_count)
		{
			extractContField(m_currPacketData, idx, field_value, 3);
			m_curr_packet.setCycleCount(field_value + m_curr_packet.getCCThreshold());
		}
		else
			m_curr_packet.setCycleCount(0);	/* unknown CC marked as 0 after overflow */
        m_process_state = SEND_PKT;
    }
}

void EtmV4IPktProcImpl::iPktSpeclRes()
{
    uint8_t lastByte = m_currPacketData.back();
    if(m_currPacketData.size() == 1)
    {
        switch(m_curr_packet.getType())
        {
        case ETM4_PKT_I_MISPREDICT:
        case ETM4_PKT_I_CANCEL_F2:
            switch(lastByte & 0x3)
            {
            case 0x1: m_curr_packet.setAtomPacket(ATOM_PATTERN, 0x1, 1); break; // E
            case 0x2: m_curr_packet.setAtomPacket(ATOM_PATTERN, 0x3, 2); break; // EE
            case 0x3: m_curr_packet.setAtomPacket(ATOM_PATTERN, 0x0, 1); break; // N
            }
            if(m_curr_packet.getType() == ETM4_PKT_I_CANCEL_F2)
                m_curr_packet.setCancelElements(1);
            m_process_state = SEND_PKT;
            break;

        case ETM4_PKT_I_CANCEL_F3:
            if(lastByte & 0x1)
                m_curr_packet.setAtomPacket(ATOM_PATTERN, 0x1, 1); // E
            m_curr_packet.setCancelElements(((lastByte >> 1) & 0x3) + 2);
            m_process_state = SEND_PKT;
            break;
        }        
    }
    else
    {
        if((lastByte & 0x80) == 0x00)
        {
            uint32_t field_val = 0;
            extractContField(m_currPacketData,1,field_val);
            if(m_curr_packet.getType() == ETM4_PKT_I_COMMIT)
                m_curr_packet.setCommitElements(field_val);
            else
                m_curr_packet.setCancelElements(field_val);
            // TBD: sanity check with max spec depth here?
            m_process_state = SEND_PKT;
        }
    }
}

void EtmV4IPktProcImpl::iPktCondInstr()   
{
    uint8_t lastByte = m_currPacketData.back();
    bool bF1Done = false;

    if(m_currPacketData.size() == 1)    
    {
        if(m_curr_packet.getType() == ETM4_PKT_I_COND_I_F2)
        {
            m_curr_packet.setCondIF2(lastByte & 0x3);
            m_process_state = SEND_PKT;
        }

    }
    else if(m_currPacketData.size() == 2)  
    {
        if(m_curr_packet.getType() == ETM4_PKT_I_COND_I_F3)   // f3 two bytes long
        {
          uint8_t num_c_elem = ((lastByte >> 1) & 0x3F) + (lastByte & 0x1);
            m_curr_packet.setCondIF3(num_c_elem,(bool)((lastByte & 0x1) == 0x1));
            // TBD: check for 0 num_c_elem in here.
            m_process_state = SEND_PKT;
        }
        else
        {
            bF1Done = ((lastByte & 0x80) == 0x00);
        }
    }
    else
    {
        bF1Done = ((lastByte & 0x80) == 0x00);
    }

    if(bF1Done)
    {
        uint32_t cond_key = 0;
        extractContField(m_currPacketData, 1, cond_key);       
        m_process_state = SEND_PKT;        
    }
}

void EtmV4IPktProcImpl::iPktCondResult()
{
    //static ocsd_etmv4_i_pkt_type format = ETM4_PKT_I_COND_RES_F1; // conditional result formats F1-F4
    uint8_t lastByte = m_currPacketData.back();
    if(m_currPacketData.size() == 1)    
    {
        m_F1P1_done = false;  // F1 payload 1 done
        m_F1P2_done = false;  // F1 payload 2 done
        m_F1has_P2 = false;   // F1 has a payload 2
                
        switch(m_curr_packet.getType())
        {
        case ETM4_PKT_I_COND_RES_F1:            

            m_F1has_P2 = true;
            if((lastByte & 0xFC) == 0x6C)// only one payload set
            {
                m_F1P2_done = true;
                m_F1has_P2 = false;
            }
            break;

        case ETM4_PKT_I_COND_RES_F2:
            m_curr_packet.setCondRF2((lastByte & 0x4) ? 2 : 1, lastByte & 0x3);
            m_process_state = SEND_PKT;
            break;

        case ETM4_PKT_I_COND_RES_F3:
            break;

        case ETM4_PKT_I_COND_RES_F4:
            m_curr_packet.setCondRF4(lastByte & 0x3);
            m_process_state = SEND_PKT;
            break;
        }        
    }
    else if((m_curr_packet.getType() == ETM4_PKT_I_COND_RES_F3) && (m_currPacketData.size() == 2)) 
    {
        // 2nd F3 packet
        uint16_t f3_tokens = 0;
        f3_tokens = (uint16_t)m_currPacketData[1];
        f3_tokens |= ((uint16_t)m_currPacketData[0] & 0xf) << 8;
        m_curr_packet.setCondRF3(f3_tokens);
        m_process_state = SEND_PKT;
    }
    else  // !first packet  - F1
    {
        if(!m_F1P1_done)
            m_F1P1_done = ((lastByte & 0x80) == 0x00);
        else if(!m_F1P2_done)
            m_F1P2_done = ((lastByte & 0x80) == 0x00);

        if(m_F1P1_done && m_F1P2_done)
        {
            int st_idx = 1;
            uint32_t key[2];
            uint8_t result[2];
            uint8_t CI[2];

            st_idx+= extractCondResult(m_currPacketData,st_idx,key[0],result[0]);
            CI[0] = m_currPacketData[0] & 0x1;
            if(m_F1has_P2) // 2nd payload?
            {
                extractCondResult(m_currPacketData,st_idx,key[1],result[1]);
                CI[1] = (m_currPacketData[0] >> 1) & 0x1;
            }
            m_curr_packet.setCondRF1(key,result,CI,m_F1has_P2);
            m_process_state = SEND_PKT;
        }
    }
}

void EtmV4IPktProcImpl::iPktContext()
{
    bool bSendPacket = false;
    uint8_t lastByte = m_currPacketData.back();
    if(m_currPacketData.size() == 1) 
    {
        if((lastByte & 0x1) == 0)
        {
            m_curr_packet.setContextInfo(false);    // no update context packet (ctxt same as last time).
            m_process_state = SEND_PKT;
        }
    }
    else if(m_currPacketData.size() == 2) 
    {
        if((lastByte & 0xC0) == 0) // no VMID or CID
        {
            bSendPacket = true;
        }
        else
        {
            m_vmidBytes = ((lastByte & 0x40) == 0x40) ? (m_config.vmidSize()/8) : 0;
            m_ctxtidBytes = ((lastByte & 0x80) == 0x80) ? (m_config.cidSize()/8) : 0;
        }
    }
    else    // 3rd byte onwards
    {
        if(m_vmidBytes > 0)
            m_vmidBytes--;
        else if(m_ctxtidBytes > 0)
            m_ctxtidBytes--;

        if((m_ctxtidBytes == 0) && (m_vmidBytes == 0))
            bSendPacket = true;        
    }

    if(bSendPacket)
    {
        extractAndSetContextInfo(m_currPacketData,1);
        m_process_state = SEND_PKT;
    }
}

void EtmV4IPktProcImpl::extractAndSetContextInfo(const std::vector<uint8_t> &buffer, const int st_idx)
{
    // on input, buffer index points at the info byte - always present
    uint8_t infoByte = m_currPacketData[st_idx];
    
    m_curr_packet.setContextInfo(true, (infoByte & 0x3), (infoByte >> 5) & 0x1, (infoByte >> 4) & 0x1);    

    // see if there are VMID and CID bytes, and how many.
    int nVMID_bytes = ((infoByte & 0x40) == 0x40) ? (m_config.vmidSize()/8) : 0;
    int nCtxtID_bytes = ((infoByte & 0x80) == 0x80) ? (m_config.cidSize()/8) : 0;

    // extract any VMID and CID
    int payload_idx = st_idx+1;
    if(nVMID_bytes)
    {
        uint32_t VMID = 0; 
        for(int i = 0; i < nVMID_bytes; i++)
        {
            VMID |= ((uint32_t)m_currPacketData[i+payload_idx] << i*8);
        }
        payload_idx += nVMID_bytes;
        m_curr_packet.setContextVMID(VMID);
    }

    if(nCtxtID_bytes)
    {
        uint32_t CID = 0; 
        for(int i = 0; i < nCtxtID_bytes; i++)
        {
            CID |= ((uint32_t)m_currPacketData[i+payload_idx] << i*8);
        }        
        m_curr_packet.setContextCID(CID);
    }
}

void EtmV4IPktProcImpl::iPktAddrCtxt()
{
    uint8_t lastByte = m_currPacketData.back();

    if( m_currPacketData.size() == 1)    
    {        
        m_addrIS = 0;
        m_addrBytes = 4;
        m_bAddr64bit = false;
        m_vmidBytes = 0;
        m_ctxtidBytes = 0;
        m_bCtxtInfoDone = false;

        switch(m_curr_packet.type)
        {
        case ETM4_PKT_I_ADDR_CTXT_L_32IS1:
            m_addrIS = 1;
        case ETM4_PKT_I_ADDR_CTXT_L_32IS0:
            break;

        case ETM4_PKT_I_ADDR_CTXT_L_64IS1:
            m_addrIS = 1;
        case ETM4_PKT_I_ADDR_CTXT_L_64IS0:
            m_addrBytes = 8;
            m_bAddr64bit = true;
            break;
        }
    }
    else
    {
        if(m_addrBytes == 0)
        {
            if(m_bCtxtInfoDone == false)
            {
                m_bCtxtInfoDone = true;
                m_vmidBytes = ((lastByte & 0x40) == 0x40) ? (m_config.vmidSize()/8) : 0;
                m_ctxtidBytes = ((lastByte & 0x80) == 0x80) ? (m_config.cidSize()/8) : 0;
            }
            else
            {
                if( m_vmidBytes > 0) 
                     m_vmidBytes--;
                else if(m_ctxtidBytes > 0)
                    m_ctxtidBytes--;
            }
        }
        else
            m_addrBytes--;

        if((m_addrBytes == 0) && m_bCtxtInfoDone && (m_vmidBytes == 0) && (m_ctxtidBytes == 0))
        {
            int st_idx = 1;
            if(m_bAddr64bit)
            {
                uint64_t val64;
                st_idx+=extract64BitLongAddr(m_currPacketData,st_idx,m_addrIS,val64);
                m_curr_packet.set64BitAddress(val64,m_addrIS);
            }
            else
            {
                uint32_t val32;
                st_idx+=extract32BitLongAddr(m_currPacketData,st_idx,m_addrIS,val32);
                m_curr_packet.set32BitAddress(val32,m_addrIS);
            }
            extractAndSetContextInfo(m_currPacketData,st_idx);
            m_process_state = SEND_PKT;
        }
    }
}

void EtmV4IPktProcImpl::iPktShortAddr()
{
    uint8_t lastByte = m_currPacketData.back();
    if(m_currPacketData.size() == 1)    
    {
        m_addr_done = false;
        m_addrIS = (lastByte == ETM4_PKT_I_ADDR_S_IS0) ? 0 : 1;
    }
    else if(!m_addr_done)
    {
        m_addr_done = (m_currPacketData.size() == 3) || ((lastByte & 0x80) == 0x00);
    }

    if(m_addr_done)
    {
        uint32_t addr_val = 0;
        int bits = 0;

        extractShortAddr(m_currPacketData,1,m_addrIS,addr_val,bits);
        m_curr_packet.updateShortAddress(addr_val,m_addrIS,(uint8_t)bits);
        m_process_state = SEND_PKT;
    }
}

int EtmV4IPktProcImpl::extractShortAddr(const std::vector<uint8_t> &buffer, const int st_idx, const uint8_t IS, uint32_t &value, int &bits)
{
    int IS_shift = (IS == 0) ? 2 : 1;
    int idx = 0;

    bits = 7;   // at least 7 bits
    value = 0;
    value |= ((uint32_t)(buffer[st_idx+idx] & 0x7F)) << IS_shift;
    
    if(m_currPacketData[st_idx+idx] & 0x80)
    {
        idx++;
        value |= ((uint32_t)m_currPacketData[st_idx+idx]) <<  (7 + IS_shift);
        bits += 8;
    }
    idx++;
    bits += IS_shift;
    return idx;
}

void EtmV4IPktProcImpl::iPktLongAddr()    
{
    if(m_currPacketData.size() == 1)    
    {
        // init the intra-byte data
        m_addrIS = 0;
        m_bAddr64bit = false;
        m_addrBytes = 4;

        switch(m_curr_packet.type)
        {
        case ETM4_PKT_I_ADDR_L_32IS1:
            m_addrIS = 1;
        case ETM4_PKT_I_ADDR_L_32IS0:
            m_addrBytes = 4;
            break;

        case ETM4_PKT_I_ADDR_L_64IS1:
            m_addrIS = 1;
        case ETM4_PKT_I_ADDR_L_64IS0:
            m_addrBytes = 8;
            m_bAddr64bit = true;
            break;
        }
    }
    if(m_currPacketData.size() == (unsigned)(1+m_addrBytes))
    {
        int st_idx = 1;
        if(m_bAddr64bit)
        {
            uint64_t val64;
            st_idx+=extract64BitLongAddr(m_currPacketData,st_idx,m_addrIS,val64);
            m_curr_packet.set64BitAddress(val64,m_addrIS);
        }
        else
        {
            uint32_t val32;
            st_idx+=extract32BitLongAddr(m_currPacketData,st_idx,m_addrIS,val32);
            m_curr_packet.set32BitAddress(val32,m_addrIS);
        }
        m_process_state = SEND_PKT;
    }
}

void EtmV4IPktProcImpl::iPktQ()
{
    uint8_t lastByte = m_currPacketData.back();

    if(m_currPacketData.size() == 1)
    {
        m_Q_type = lastByte & 0xF;

        m_addrBytes = 0;
        m_count_done = false;
        m_has_addr = false;
        m_addr_short = true;
        m_addr_match = false;
        m_addrIS = 1;
        m_QE = 0;

        switch(m_Q_type)
        {
            // count only - implied address.
        case 0x0:
        case 0x1:
        case 0x2:
            m_addr_match = true;
            m_has_addr = true;
            m_QE = m_Q_type & 0x3;
        case 0xC:
            break;

            // count + short address 
        case 0x5:
            m_addrIS = 0;
        case 0x6:
            m_has_addr = true;            
            m_addrBytes = 2;  // short IS0/1
            break;

            // count + long address
        case 0xA:
            m_addrIS = 0;
        case 0xB:
            m_has_addr = true;
            m_addr_short = false;
            m_addrBytes = 4; // long IS0/1
            break;

            // no count, no address
        case 0xF:
            m_count_done = true;
            break;

            // reserved values 0x3, 0x4, 0x7, 0x8, 0x9, 0xD, 0xE
        default:
            m_curr_packet.err_type =  m_curr_packet.type;
            m_curr_packet.type = ETM4_PKT_I_BAD_SEQUENCE;
            //SendBadIPacket( PKT_BAD_SEQUENCE, "ERROR: Bad Q packet type", PKT_Q );
            break;
        }
    }
    else
    {
        if(m_addrBytes > 0)
        {
            if(m_addr_short && m_addrBytes == 2)  // short
            {
                if((lastByte & 0x80) == 0x00)
                    m_addrBytes--;        // short version can have just single byte.
            }
            m_addrBytes--;
        }
        else if(!m_count_done)
        {
            m_count_done = ((lastByte & 0x80) == 0x00);
        }
    }

    if(((m_addrBytes == 0) && m_count_done))
    {
        int idx = 1; // move past the header
        int bits = 0;
        uint32_t q_addr;
        uint32_t q_count;

        if(m_has_addr)
        {
            if(m_addr_match)
            {
                m_curr_packet.setAddressExactMatch(m_QE);
            }
            else if(m_addr_short)
            {
                idx+=extractShortAddr(m_currPacketData,idx,m_addrIS,q_addr,bits);
                m_curr_packet.updateShortAddress(q_addr,m_addrIS,(uint8_t)bits);
            }
            else
            {
                idx+=extract32BitLongAddr(m_currPacketData,idx,m_addrIS,q_addr);
                m_curr_packet.set32BitAddress(q_addr,m_addrIS);
            }
        }

        if(m_Q_type != 0xF)
        {
            extractContField(m_currPacketData,idx,q_count);
            m_curr_packet.setQType(true,q_count,m_has_addr,m_addr_match,m_Q_type);
        }
        else
        {
            m_curr_packet.setQType(false,0,false,false,0xF);
        }
        m_process_state = SEND_PKT;
    }

}

void EtmV4IPktProcImpl::iAtom()
{
    // patterns lsbit = oldest atom, ms bit = newest.
    static const uint32_t f4_patterns[] = {
        0xE, // EEEN 
        0x0, // NNNN
        0xA, // ENEN
        0x5  // NENE
    };

    uint8_t lastByte = m_currPacketData.back();
    uint8_t pattIdx = 0, pattCount = 0;
    uint32_t pattern;

    // atom packets are single byte, no payload.
    switch(m_curr_packet.type)
    {
    case ETM4_PKT_I_ATOM_F1:
        m_curr_packet.setAtomPacket(ATOM_PATTERN,(lastByte & 0x1), 1); // 1xE or N
        break;

    case ETM4_PKT_I_ATOM_F2:
        m_curr_packet.setAtomPacket(ATOM_PATTERN,(lastByte & 0x3), 2); // 2x (E or N)
        break;

    case ETM4_PKT_I_ATOM_F3:
        m_curr_packet.setAtomPacket(ATOM_PATTERN,(lastByte & 0x7), 3); // 3x (E or N)
        break;

    case ETM4_PKT_I_ATOM_F4:
        m_curr_packet.setAtomPacket(ATOM_PATTERN,f4_patterns[(lastByte & 0x3)], 4); // 4 atom pattern
        break; 

    case ETM4_PKT_I_ATOM_F5:
        pattIdx = ((lastByte & 0x20) >> 3) | (lastByte & 0x3);
        switch(pattIdx)
        {
        case 5: // 0b101
            m_curr_packet.setAtomPacket(ATOM_PATTERN,0x1E, 5); // 5 atom pattern EEEEN
            break;

        case 1: // 0b001
            m_curr_packet.setAtomPacket(ATOM_PATTERN,0x00, 5); // 5 atom pattern NNNNN
            break;

        case 2: //0b010
            m_curr_packet.setAtomPacket(ATOM_PATTERN,0x0A, 5); // 5 atom pattern NENEN
            break;

        case 3: //0b011
            m_curr_packet.setAtomPacket(ATOM_PATTERN,0x15, 5); // 5 atom pattern ENENE
            break;

        default:
            // TBD: warn about invalid pattern in here.
            break;
        }
        break;

    case ETM4_PKT_I_ATOM_F6:
        pattCount = (lastByte & 0x1F) + 3;  // count of E's
        // TBD: check 23 or less at this point? 
        pattern = ((uint32_t)0x1 << pattCount) - 1; // set pattern to string of E's
        if((lastByte & 0x20) == 0x00)   // last atom is E?
            pattern |= ((uint32_t)0x1 << pattCount); 
        m_curr_packet.setAtomPacket(ATOM_PATTERN,pattern, pattCount+1);
        break;
    }

    m_process_state = SEND_PKT;
}

// header byte processing is table driven.
void EtmV4IPktProcImpl::BuildIPacketTable()   
{
    // initialise everything as reserved.
    for(int i = 0; i < 256; i++)
    {
        m_i_table[i].pkt_type = ETM4_PKT_I_RESERVED;
        m_i_table[i].pptkFn = &EtmV4IPktProcImpl::iPktReserved;
    }

    // 0x00 - extension 
    m_i_table[0x00].pkt_type = ETM4_PKT_I_EXTENSION;
    m_i_table[0x00].pptkFn   = &EtmV4IPktProcImpl::iPktExtension;

    // 0x01 - Trace info
    m_i_table[0x01].pkt_type = ETM4_PKT_I_TRACE_INFO;
    m_i_table[0x01].pptkFn   = &EtmV4IPktProcImpl::iPktTraceInfo;

    // b0000001x - timestamp
    m_i_table[0x02].pkt_type = ETM4_PKT_I_TIMESTAMP;
    m_i_table[0x02].pptkFn   = &EtmV4IPktProcImpl::iPktTimestamp;
    m_i_table[0x03].pkt_type = ETM4_PKT_I_TIMESTAMP;
    m_i_table[0x03].pptkFn   = &EtmV4IPktProcImpl::iPktTimestamp;

    // b0000 0100 - trace on
    m_i_table[0x04].pkt_type = ETM4_PKT_I_TRACE_ON;
    m_i_table[0x04].pptkFn   = &EtmV4IPktProcImpl::iPktNoPayload;

    // b0000 0110 - exception 
    m_i_table[0x06].pkt_type = ETM4_PKT_I_EXCEPT;
    m_i_table[0x06].pptkFn   = &EtmV4IPktProcImpl::iPktException;

    // b0000 0111 - exception return 
    m_i_table[0x07].pkt_type = ETM4_PKT_I_EXCEPT_RTN;
    m_i_table[0x07].pptkFn   = &EtmV4IPktProcImpl::iPktNoPayload;

    // b0000 110x - cycle count f2
    // b0000 111x - cycle count f1
    for(int i = 0; i < 4; i++)
    {
        m_i_table[0x0C+i].pkt_type = (i >= 2) ? ETM4_PKT_I_CCNT_F1 : ETM4_PKT_I_CCNT_F2;
        m_i_table[0x0C+i].pptkFn   = &EtmV4IPktProcImpl::iPktCycleCntF123;
    }

    // b0001 xxxx - cycle count f3
    for(int i = 0; i < 16; i++)
    {
        m_i_table[0x10+i].pkt_type = ETM4_PKT_I_CCNT_F3;
        m_i_table[0x10+i].pptkFn   = &EtmV4IPktProcImpl::iPktCycleCntF123;
    }

    // b0010 0xxx - NDSM
    for(int i = 0; i < 8; i++)
    {
        m_i_table[0x20+i].pkt_type = ETM4_PKT_I_NUM_DS_MKR;
        m_i_table[0x20+i].pptkFn   = &EtmV4IPktProcImpl::iPktNoPayload;
    }

    // b0010 10xx, b0010 1100 - UDSM
    for(int i = 0; i < 5; i++)
    {
        m_i_table[0x28+i].pkt_type = ETM4_PKT_I_UNNUM_DS_MKR;
        m_i_table[0x28+i].pptkFn   = &EtmV4IPktProcImpl::iPktNoPayload;
    }

    // b0010 1101 - commit
    m_i_table[0x2D].pkt_type = ETM4_PKT_I_COMMIT;
    m_i_table[0x2D].pptkFn   = &EtmV4IPktProcImpl::iPktSpeclRes;


    // b0010 111x - cancel f1
    for(int i = 0; i < 2; i++)
    {
        // G++ doesn't understand [0x2E+i] so...
        int idx = i + 0x2E;
        m_i_table[idx].pkt_type = ETM4_PKT_I_CANCEL_F1;
        m_i_table[idx].pptkFn   = &EtmV4IPktProcImpl::iPktSpeclRes;
    }

    // b0011 00xx - mis predict
    for(int i = 0; i < 4; i++)
    {
        m_i_table[0x30+i].pkt_type = ETM4_PKT_I_MISPREDICT;
        m_i_table[0x30+i].pptkFn   =  &EtmV4IPktProcImpl::iPktSpeclRes;
    }

    // b0011 01xx - cancel f2
    for(int i = 0; i < 4; i++)
    {
        m_i_table[0x34+i].pkt_type = ETM4_PKT_I_CANCEL_F2;
        m_i_table[0x34+i].pptkFn   =  &EtmV4IPktProcImpl::iPktSpeclRes;
    }

    // b0011 1xxx - cancel f3
    for(int i = 0; i < 8; i++)
    {
        m_i_table[0x38+i].pkt_type = ETM4_PKT_I_CANCEL_F3;
        m_i_table[0x38+i].pptkFn   =  &EtmV4IPktProcImpl::iPktSpeclRes;
    }

    // b0100 000x, b0100 0010 - cond I f2
    for(int i = 0; i < 3; i++)
    {
        m_i_table[0x40+i].pkt_type = ETM4_PKT_I_COND_I_F2;
        m_i_table[0x40+i].pptkFn   = &EtmV4IPktProcImpl::iPktCondInstr;
    }

    // b0100 0011 - cond flush
    m_i_table[0x43].pkt_type = ETM4_PKT_I_COND_FLUSH;
    m_i_table[0x43].pptkFn   = &EtmV4IPktProcImpl::iPktNoPayload;

    // b0100 010x, b0100 0110 - cond res f4
    for(int i = 0; i < 3; i++)
    {
        m_i_table[0x44+i].pkt_type = ETM4_PKT_I_COND_RES_F4;
        m_i_table[0x44+i].pptkFn   = &EtmV4IPktProcImpl::iPktCondResult;
    }

    // b0100 100x, b0100 0110 - cond res f2
    // b0100 110x, b0100 1110 - cond res f2
    for(int i = 0; i < 3; i++)
    {
        m_i_table[0x48+i].pkt_type = ETM4_PKT_I_COND_RES_F2;
        m_i_table[0x48+i].pptkFn   = &EtmV4IPktProcImpl::iPktCondResult;
    }
    for(int i = 0; i < 3; i++)
    {
        m_i_table[0x4C+i].pkt_type = ETM4_PKT_I_COND_RES_F2;
        m_i_table[0x4C+i].pptkFn   = &EtmV4IPktProcImpl::iPktCondResult;
    }

    // b0101xxxx - cond res f3
    for(int i = 0; i < 16; i++)
    {
        m_i_table[0x50+i].pkt_type = ETM4_PKT_I_COND_RES_F3;
        m_i_table[0x50+i].pptkFn   = &EtmV4IPktProcImpl::iPktCondResult;
    }

    // b011010xx - cond res f1
    for(int i = 0; i < 4; i++)
    {
        m_i_table[0x68+i].pkt_type = ETM4_PKT_I_COND_RES_F1;
        m_i_table[0x68+i].pptkFn   = &EtmV4IPktProcImpl::iPktCondResult;
    }

    // b0110 1100 - cond instr f1
    m_i_table[0x6C].pkt_type = ETM4_PKT_I_COND_I_F1;
    m_i_table[0x6C].pptkFn   = &EtmV4IPktProcImpl::iPktCondInstr;

    // b0110 1101 - cond instr f3
    m_i_table[0x6D].pkt_type = ETM4_PKT_I_COND_I_F3;
    m_i_table[0x6D].pptkFn   = &EtmV4IPktProcImpl::iPktCondInstr;

    // b0110111x - cond res f1
    for(int i = 0; i < 2; i++)
    {
        // G++ cannot understand [0x6E+i] so change these round
        m_i_table[i+0x6E].pkt_type = ETM4_PKT_I_COND_RES_F1;
        m_i_table[i+0x6E].pptkFn   = &EtmV4IPktProcImpl::iPktCondResult;
    }

    // b01110001 - b01111111 - cond res f1
    for(int i = 0; i < 15; i++)
    {
        m_i_table[0x71+i].pkt_type = ETM4_PKT_I_EVENT;
        m_i_table[0x71+i].pptkFn   = &EtmV4IPktProcImpl::iPktNoPayload;
    }
    
    // 0b1000 000x - context 
    for(int i = 0; i < 2; i++)
    {
        m_i_table[0x80+i].pkt_type = ETM4_PKT_I_CTXT;
        m_i_table[0x80+i].pptkFn   = &EtmV4IPktProcImpl::iPktContext;
    }

    // 0b1000 0010 to b1000 0011 - addr with ctxt
    // 0b1000 0101 to b1000 0110 - addr with ctxt
    for(int i = 0; i < 2; i++)
    {
        m_i_table[0x82+i].pkt_type =  (i == 0) ? ETM4_PKT_I_ADDR_CTXT_L_32IS0 : ETM4_PKT_I_ADDR_CTXT_L_32IS1;
        m_i_table[0x82+i].pptkFn   = &EtmV4IPktProcImpl::iPktAddrCtxt;
    }

    for(int i = 0; i < 2; i++)
    {
        m_i_table[0x85+i].pkt_type = (i == 0) ? ETM4_PKT_I_ADDR_CTXT_L_64IS0 : ETM4_PKT_I_ADDR_CTXT_L_64IS1;
        m_i_table[0x85+i].pptkFn   = &EtmV4IPktProcImpl::iPktAddrCtxt;
    }

    // 0b1001 0000 to b1001 0010 - exact match addr
    for(int i = 0; i < 3; i++)
    {
        m_i_table[0x90+i].pkt_type = ETM4_PKT_I_ADDR_MATCH;
        m_i_table[0x90+i].pptkFn   = &EtmV4IPktProcImpl::iPktNoPayload;
    }

    // b1001 0101 - b1001 0110 - addr short address
    for(int i = 0; i < 2; i++)
    {
        m_i_table[0x95+i].pkt_type =  (i == 0) ? ETM4_PKT_I_ADDR_S_IS0 : ETM4_PKT_I_ADDR_S_IS1;
        m_i_table[0x95+i].pptkFn   = &EtmV4IPktProcImpl::iPktShortAddr;
    }

    // b10011010 - b10011011 - addr long address 
    // b10011101 - b10011110 - addr long address 
    for(int i = 0; i < 2; i++)
    {
        m_i_table[0x9A+i].pkt_type =  (i == 0) ? ETM4_PKT_I_ADDR_L_32IS0 : ETM4_PKT_I_ADDR_L_32IS1;
        m_i_table[0x9A+i].pptkFn   = &EtmV4IPktProcImpl::iPktLongAddr;
    }
    for(int i = 0; i < 2; i++)
    {
        m_i_table[0x9D+i].pkt_type =  (i == 0) ? ETM4_PKT_I_ADDR_L_64IS0 : ETM4_PKT_I_ADDR_L_64IS1;
        m_i_table[0x9D+i].pptkFn   = &EtmV4IPktProcImpl::iPktLongAddr;
    }

    // b1010xxxx - Q packet
    for(int i = 0; i < 16; i++)
    {
        m_i_table[0xA0+i].pkt_type = ETM4_PKT_I_Q;
        m_i_table[0xA0+i].pptkFn   = &EtmV4IPktProcImpl::iPktQ;
    }

    // Atom Packets - all no payload but have specific pattern generation fn
    for(int i = 0xC0; i <= 0xD4; i++)   // atom f6
    {
        m_i_table[i].pkt_type = ETM4_PKT_I_ATOM_F6;
        m_i_table[i].pptkFn   = &EtmV4IPktProcImpl::iAtom;
    }
    for(int i = 0xD5; i <= 0xD7; i++)  // atom f5
    {
        m_i_table[i].pkt_type = ETM4_PKT_I_ATOM_F5;
        m_i_table[i].pptkFn   = &EtmV4IPktProcImpl::iAtom;
    }
    for(int i = 0xD8; i <= 0xDB; i++)  // atom f2
    {
        m_i_table[i].pkt_type = ETM4_PKT_I_ATOM_F2;
        m_i_table[i].pptkFn   = &EtmV4IPktProcImpl::iAtom;
    }
    for(int i = 0xDC; i <= 0xDF; i++)  // atom f4
    {
        m_i_table[i].pkt_type = ETM4_PKT_I_ATOM_F4;
        m_i_table[i].pptkFn   = &EtmV4IPktProcImpl::iAtom;
    }
    for(int i = 0xE0; i <= 0xF4; i++)  // atom f6
    {
        m_i_table[i].pkt_type = ETM4_PKT_I_ATOM_F6;
        m_i_table[i].pptkFn   = &EtmV4IPktProcImpl::iAtom;
    }
    
    // atom f5
    m_i_table[0xF5].pkt_type = ETM4_PKT_I_ATOM_F5;
    m_i_table[0xF5].pptkFn   = &EtmV4IPktProcImpl::iAtom;

    for(int i = 0xF6; i <= 0xF7; i++)  // atom f1
    {
        m_i_table[i].pkt_type = ETM4_PKT_I_ATOM_F1;
        m_i_table[i].pptkFn   = &EtmV4IPktProcImpl::iAtom;
    }
    for(int i = 0xF8; i <= 0xFF; i++)  // atom f3
    {
        m_i_table[i].pkt_type = ETM4_PKT_I_ATOM_F3;
        m_i_table[i].pptkFn   = &EtmV4IPktProcImpl::iAtom;
    }
}

 unsigned EtmV4IPktProcImpl::extractContField(const std::vector<uint8_t> &buffer, const unsigned st_idx, uint32_t &value, const unsigned byte_limit /*= 5*/)
{
    unsigned idx = 0;
    bool lastByte = false;
    uint8_t byteVal;
    value = 0;
    while(!lastByte && (idx < byte_limit))   // max 5 bytes for 32 bit value;
    {
        if(buffer.size() > (st_idx + idx))
        {
            // each byte has seven bits + cont bit
            byteVal = buffer[(st_idx + idx)];
            lastByte = (byteVal & 0x80) != 0x80;
            value |= ((uint32_t)(byteVal & 0x7F)) << (idx * 7);
            idx++;
        }
        else
        {
            throwBadSequenceError("Invalid 32 bit continuation fields in packet");
        }
    }
    return idx;
}

unsigned EtmV4IPktProcImpl::extractContField64(const std::vector<uint8_t> &buffer, const unsigned st_idx, uint64_t &value, const unsigned byte_limit /*= 9*/)
{
    unsigned idx = 0;
    bool lastByte = false;
    uint8_t byteVal;
    value = 0;
    while(!lastByte && (idx < byte_limit))   // max 9 bytes for 64 bit value;
    {
        if(buffer.size() > (st_idx + idx))
        {
            // each byte has seven bits + cont bit
            byteVal = buffer[(st_idx + idx)];
            lastByte = (byteVal & 0x80) != 0x80;
            value |= ((uint64_t)(byteVal & 0x7F)) << (idx * 7);
            idx++;
        }
        else
        {
            throwBadSequenceError("Invalid 64 bit continuation fields in packet");
        }
    }
    return idx;
}

 unsigned EtmV4IPktProcImpl::extractCondResult(const std::vector<uint8_t> &buffer, const unsigned st_idx, uint32_t& key, uint8_t &result)
{
    unsigned idx = 0;
    bool lastByte = false;
    int incr = 0;

    key = 0;

    while(!lastByte && (idx < 6)) // cannot be more than 6 bytes for res + 32 bit key
    {
        if(buffer.size() > (st_idx + idx))
        {
            if(idx == 0)
            {
                result = buffer[st_idx+idx];
                key = (buffer[st_idx+idx] >> 4) & 0x7;
                incr+=3;
            }
            else
            {
                key |= ((uint32_t)(buffer[st_idx+idx] & 0x7F)) << incr;
                incr+=7;
            }
            lastByte = (bool)((buffer[st_idx+idx] & 0x80) == 0); 
            idx++;
        }
        else
        {
            throwBadSequenceError("Invalid continuation fields in packet");
        }
    }    
    return idx;
}

int EtmV4IPktProcImpl::extract64BitLongAddr(const std::vector<uint8_t> &buffer, const int st_idx, const uint8_t IS, uint64_t &value)
{
    value = 0;
    if(IS == 0)
    {
        value |= ((uint64_t)(buffer[st_idx+0] & 0x7F)) << 2;
        value |= ((uint64_t)(buffer[st_idx+1] & 0x7F)) << 9;
    }
    else
    {
        value |= ((uint64_t)(buffer[st_idx+0] & 0x7F)) << 1;
        value |= ((uint64_t)buffer[st_idx+1]) << 8;
    }
    value |= ((uint64_t)buffer[st_idx+2]) << 16;
    value |= ((uint64_t)buffer[st_idx+3]) << 24;
    value |= ((uint64_t)buffer[st_idx+4]) << 32;
    value |= ((uint64_t)buffer[st_idx+5]) << 40;
    value |= ((uint64_t)buffer[st_idx+6]) << 48;
    value |= ((uint64_t)buffer[st_idx+7]) << 56;      
    return 8;    
}

int EtmV4IPktProcImpl::extract32BitLongAddr(const std::vector<uint8_t> &buffer, const int st_idx, const uint8_t IS, uint32_t &value)
{
    value = 0;
    if(IS == 0)
    {
        value |= ((uint32_t)(buffer[st_idx+0] & 0x7F)) << 2;
        value |= ((uint32_t)(buffer[st_idx+1] & 0x7F)) << 9;
    }
    else
    {
        value |= ((uint32_t)(buffer[st_idx+0] & 0x7F)) << 1;
        value |= ((uint32_t)buffer[st_idx+1]) << 8;
    }
    value |= ((uint32_t)buffer[st_idx+2]) << 16;
    value |= ((uint32_t)buffer[st_idx+3]) << 24;
    return 4;
}

void EtmV4IPktProcImpl::throwBadSequenceError(const char *pszExtMsg)
{
    m_curr_packet.updateErrType(ETM4_PKT_I_BAD_SEQUENCE);   // swap type for err type
    throw ocsdError(OCSD_ERR_SEV_ERROR, OCSD_ERR_BAD_PACKET_SEQ,m_packet_index,m_config.getTraceID(),pszExtMsg);
}


/* End of File trc_pkt_proc_etmv4i_impl.cpp */
