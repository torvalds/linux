/*
 * \file       trc_pkt_proc_stm.cpp
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

#include "opencsd/stm/trc_pkt_proc_stm.h"


// processor object construction
// ************************

#ifdef __GNUC__
// G++ doesn't like the ## pasting
#define STM_PKTS_NAME "PKTP_STM"
#else
#define STM_PKTS_NAME OCSD_CMPNAME_PREFIX_PKTPROC##"_STM"
#endif

static const uint32_t STM_SUPPORTED_OP_FLAGS = OCSD_OPFLG_PKTPROC_COMMON;

TrcPktProcStm::TrcPktProcStm() : TrcPktProcBase(STM_PKTS_NAME)
{
    initObj();
}

TrcPktProcStm::TrcPktProcStm(int instIDNum) : TrcPktProcBase(STM_PKTS_NAME, instIDNum)
{
    initObj();
}

TrcPktProcStm::~TrcPktProcStm() 
{
    getRawPacketMonAttachPt()->set_notifier(0);
}

void TrcPktProcStm::initObj()
{
    m_supported_op_flags = STM_SUPPORTED_OP_FLAGS;
    initProcessorState();
    getRawPacketMonAttachPt()->set_notifier(&mon_in_use);
    buildOpTables();
}

// implementation packet processing interface overrides 
// ************************
ocsd_datapath_resp_t TrcPktProcStm::processData(  const ocsd_trc_index_t index,
                                            const uint32_t dataBlockSize,
                                            const uint8_t *pDataBlock,
                                            uint32_t *numBytesProcessed)
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    m_p_data_in = pDataBlock;
    m_data_in_size = dataBlockSize;
    m_data_in_used = 0;
   
    // while there is data and a continue response on the data path
    while(  dataToProcess() && OCSD_DATA_RESP_IS_CONT(resp) )
    {
        try 
        {
            switch(m_proc_state)
            {
            case WAIT_SYNC:
                waitForSync(index);
                break;

            case PROC_HDR:
                m_packet_index = index + m_data_in_used;
                if(readNibble())
                {
                    m_proc_state = PROC_DATA;   // read the header nibble, next if any has to be data
                    m_pCurrPktFn = m_1N_ops[m_nibble]; // set packet function and fall through                    
                }
                else
                    break;

            case PROC_DATA:
                (this->*m_pCurrPktFn)();

                // if we have enough to send, fall through, otherwise stop
                if(m_proc_state != SEND_PKT)
                    break;

            case SEND_PKT:
                resp = outputPacket();
                break;
            }
        }
        catch(ocsdError &err)
        {
            LogError(err);
            if( ((err.getErrorCode() == OCSD_ERR_BAD_PACKET_SEQ) ||
                 (err.getErrorCode() == OCSD_ERR_INVALID_PCKT_HDR)) &&
                 !(getComponentOpMode() & OCSD_OPFLG_PKTPROC_ERR_BAD_PKTS))
            {
                // send invalid packets up the pipe to let the next stage decide what to do.
                resp = outputPacket();
                if(getComponentOpMode() & OCSD_OPFLG_PKTPROC_UNSYNC_ON_BAD_PKTS)
                    m_proc_state = WAIT_SYNC;
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
            ocsdError fatal = ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_FAIL,m_packet_index,m_config->getTraceID());
            fatal.setMessage("Unknown System Error decoding trace.");
            LogError(fatal);
        }
    }
       
    *numBytesProcessed = m_data_in_used;
    return resp;
    
}

ocsd_datapath_resp_t TrcPktProcStm::onEOT()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    if(m_num_nibbles > 0)   // there is a partial packet in flight
    {
        m_curr_packet.updateErrType(STM_PKT_INCOMPLETE_EOT);    // re mark as incomplete
        resp = outputPacket();
    }
    return resp;
}

ocsd_datapath_resp_t TrcPktProcStm::onReset()
{
    initProcessorState();
    return OCSD_RESP_CONT;
}

ocsd_datapath_resp_t TrcPktProcStm::onFlush()
{
    // packet processor never holds on to flushable data (may have partial packet, 
    // but any full packets are immediately sent)
    return OCSD_RESP_CONT;
}

ocsd_err_t TrcPktProcStm::onProtocolConfig()
{
    return OCSD_OK;  // nothing to do on config for this processor
}

const bool TrcPktProcStm::isBadPacket() const
{
    return m_curr_packet.isBadPacket();
}

ocsd_datapath_resp_t TrcPktProcStm::outputPacket()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    resp = outputOnAllInterfaces(m_packet_index,&m_curr_packet,&m_curr_packet.type,m_packet_data);
    m_packet_data.clear();
    initNextPacket();
    if(m_nibble_2nd_valid)
        savePacketByte(m_nibble_2nd << 4);     // put the unused nibble back on to the data stack and pad for output next time.
    m_proc_state = m_bStreamSync ? PROC_HDR : WAIT_SYNC;
    return resp;
}

void TrcPktProcStm::throwBadSequenceError(const char *pszMessage /*= ""*/)
{
    m_curr_packet.updateErrType(STM_PKT_BAD_SEQUENCE);
    throw ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_BAD_PACKET_SEQ,m_packet_index,this->m_config->getTraceID(),pszMessage);
}

void TrcPktProcStm::throwReservedHdrError(const char *pszMessage /*= ""*/)
{
    m_curr_packet.setPacketType(STM_PKT_RESERVED,false);
    throw ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_INVALID_PCKT_HDR,m_packet_index,this->m_config->getTraceID(),pszMessage);
}

// processor / packet init
// ************************

void TrcPktProcStm::initProcessorState()
{
    // clear any state that persists between packets
    setProcUnsynced();
    clearSyncCount();
    m_curr_packet.initStartState();
    m_nibble_2nd_valid = false;
    initNextPacket();
    m_bWaitSyncSaveSuppressed = false;

    m_packet_data.clear();
}

void TrcPktProcStm::initNextPacket()
{
    // clear state that is unique to each packet
    m_bNeedsTS = false;
    m_bIsMarker = false;
    m_num_nibbles = 0;
    m_num_data_nibbles = 0;
    m_curr_packet.initNextPacket();
}

// search remaining buffer for a start of sync or full sync packet
void TrcPktProcStm::waitForSync(const ocsd_trc_index_t blk_st_index)
{
    bool bGotData = true;
    uint32_t start_offset = m_data_in_used; // record the offset into the buffer at start of this fn.

    // input conditions:
    // out of sync - either at start of input stream, or due to bad packet.
    // m_data_in_used -> bytes already processed
    // m_sync_start -> seen potential start of sync in current stream

    // set a packet index for the start of the data
    m_packet_index = blk_st_index + m_data_in_used;
    m_num_nibbles = m_is_sync ? m_num_F_nibbles + 1 : m_num_F_nibbles;    // sending unsync data may have cleared down num_nibbles.

    m_bWaitSyncSaveSuppressed = true;   // no need to save bytes until we want to send data.

    while(bGotData && !m_is_sync)
    {
        bGotData = readNibble();    // read until we have a sync or run out of data
    }

    m_bWaitSyncSaveSuppressed = false;

    // no data from first attempt to read
    if(m_num_nibbles == 0)
        return;
    
    // we have found a sync or run out of data
    // five possible scenarios
    // a) all data none sync data.
    // b) some none sync data + start of sync sequence
    // c) some none sync data + full sync sequence in this frame
    // d) full sync sequence @ start of this frame followed by ???
    // e) completion of sync sequence in this frame (from b)).

    if(!bGotData || m_num_nibbles > 22)
    {
        // for a), b), c) send the none sync data then re-enter
        // if out of data, or sync with some previous data, this is sent as unsynced.
        
        m_curr_packet.setPacketType(STM_PKT_NOTSYNC,false);
        if(mon_in_use.usingMonitor())
        {
            uint8_t nibbles_to_send = m_num_nibbles - (m_is_sync ? 22 : m_num_F_nibbles);
            uint8_t bytes_to_send = (nibbles_to_send / 2) + (nibbles_to_send % 2);
            for(uint8_t i = 0; i < bytes_to_send; i++)
                savePacketByte(m_p_data_in[start_offset+i]);
        }

        // if we have found a sync then we will re-enter this function with no pre data, 
        // but the found flags set.
    }
    else
    {
        // send the async packet
        m_curr_packet.setPacketType(STM_PKT_ASYNC,false);
        m_bStreamSync = true;   // mark the stream as synchronised
        clearSyncCount();
        m_packet_index = m_sync_index;
        if(mon_in_use.usingMonitor())
        {
            // we may not have the full sync packet still in the local buffer so synthesise it.
            for(int i = 0; i < 10; i++)
                savePacketByte(0xFF);
            savePacketByte(0x0F);
        }        
    }
    sendPacket();  // mark packet for sending
}

// packet processing routines
// ************************
// 1 nibble opcodes
void TrcPktProcStm::stmPktReserved()
{
    uint16_t bad_opcode = (uint16_t)m_nibble;    
    m_curr_packet.setD16Payload(bad_opcode);
    throwReservedHdrError("STM: Unsupported or Reserved STPv2 Header");
}

void TrcPktProcStm::stmPktNull()
{
    m_curr_packet.setPacketType(STM_PKT_NULL,false);
    if(m_bNeedsTS)
    {
        m_pCurrPktFn = &TrcPktProcStm::stmExtractTS;
        (this->*m_pCurrPktFn)();
    }
    else
    {
        sendPacket();
    }
}

void TrcPktProcStm::stmPktNullTS()
{
    pktNeedsTS();
    m_pCurrPktFn = &TrcPktProcStm::stmPktNull;
    (this->*m_pCurrPktFn)();
}

void TrcPktProcStm::stmPktM8()
{
    if(m_num_nibbles == 1)    // 1st nibble - header - set type
        m_curr_packet.setPacketType(STM_PKT_M8,false);

    stmExtractVal8(3);
    if(m_num_nibbles == 3)
    {
        m_curr_packet.setMaster(m_val8);
        sendPacket();
    }
}

void TrcPktProcStm::stmPktMERR()
{
    if(m_num_nibbles == 1)    // 1st nibble - header - set type
        m_curr_packet.setPacketType(STM_PKT_MERR,false);

    stmExtractVal8(3);
    if(m_num_nibbles == 3)
    {
        m_curr_packet.setChannel(0,false);    // MERR resets channel for current master to 0.
        m_curr_packet.setD8Payload(m_val8);
        sendPacket();
    }

}

void TrcPktProcStm::stmPktC8()
{
    if(m_num_nibbles == 1)    // 1st nibble - header - set type
        m_curr_packet.setPacketType(STM_PKT_C8,false);
    stmExtractVal8(3);
    if(m_num_nibbles == 3)
    {
        m_curr_packet.setChannel((uint16_t)m_val8,true);
        sendPacket();
    }
}

void TrcPktProcStm::stmPktD4()
{
    if(m_num_nibbles == 1)    // 1st nibble - header - set type
    {
        m_curr_packet.setPacketType(STM_PKT_D4,m_bIsMarker);
        m_num_data_nibbles = 2;  // need 2 nibbles to complete data
    }

    if(m_num_nibbles != m_num_data_nibbles)
    {
        if(readNibble())
        {
            m_curr_packet.setD4Payload(m_nibble);
            if(m_bNeedsTS)
            {
                m_pCurrPktFn = &TrcPktProcStm::stmExtractTS;
                (this->*m_pCurrPktFn)();
            }
            else
                sendPacket();
        }
    }
}

void TrcPktProcStm::stmPktD8()
{
    if(m_num_nibbles == 1)    // 1st nibble - header - set type
    {
        m_curr_packet.setPacketType(STM_PKT_D8,m_bIsMarker);
        m_num_data_nibbles = 3; // need 3 nibbles in total to complete data
    }

    stmExtractVal8(m_num_data_nibbles);
    if(m_num_nibbles == m_num_data_nibbles)
    {
        m_curr_packet.setD8Payload(m_val8);
        if(m_bNeedsTS)
        {
            m_pCurrPktFn = &TrcPktProcStm::stmExtractTS;
            (this->*m_pCurrPktFn)();
        }
        else
        {
            sendPacket();
        }
    }
}

void TrcPktProcStm::stmPktD16()
{
    if(m_num_nibbles == 1)    // 1st nibble - header - set type
    {
        m_curr_packet.setPacketType(STM_PKT_D16,m_bIsMarker);
        m_num_data_nibbles = 5;
    }

    stmExtractVal16(m_num_data_nibbles);
    if(m_num_nibbles == m_num_data_nibbles)
    {
        m_curr_packet.setD16Payload(m_val16);
        if(m_bNeedsTS)
        {
            m_pCurrPktFn = &TrcPktProcStm::stmExtractTS;
            (this->*m_pCurrPktFn)();
        }
        else
        {
            sendPacket();
        }
    }
}

void TrcPktProcStm::stmPktD32()
{
    if(m_num_nibbles == 1)    // 1st nibble - header - set type
    {
        m_curr_packet.setPacketType(STM_PKT_D32,m_bIsMarker);
        m_num_data_nibbles = 9;
    }

    stmExtractVal32(m_num_data_nibbles);
    if(m_num_nibbles == m_num_data_nibbles)
    {
        m_curr_packet.setD32Payload(m_val32);
        if(m_bNeedsTS)
        {
            m_pCurrPktFn = &TrcPktProcStm::stmExtractTS;
            (this->*m_pCurrPktFn)();
        }
        else
        {
            sendPacket();
        }
    }
}

void TrcPktProcStm::stmPktD64()
{
    if(m_num_nibbles == 1)    // 1st nibble - header - set type
    {
        m_curr_packet.setPacketType(STM_PKT_D64,m_bIsMarker);
        m_num_data_nibbles = 17;
    }

    stmExtractVal64(m_num_data_nibbles);
    if(m_num_nibbles == m_num_data_nibbles)
    {
        m_curr_packet.setD64Payload(m_val64);
        if(m_bNeedsTS)
        {
            m_pCurrPktFn = &TrcPktProcStm::stmExtractTS;
            (this->*m_pCurrPktFn)();
        }
        else
        {
            sendPacket();
        }
    }
}

void TrcPktProcStm::stmPktD4MTS()
{
    pktNeedsTS();
    m_bIsMarker = true;  
    m_pCurrPktFn = &TrcPktProcStm::stmPktD4;
    (this->*m_pCurrPktFn)();
}

void TrcPktProcStm::stmPktD8MTS()
{
    pktNeedsTS();
    m_bIsMarker = true;  
    m_pCurrPktFn = &TrcPktProcStm::stmPktD8;
    (this->*m_pCurrPktFn)();
}

void TrcPktProcStm::stmPktD16MTS()
{
    pktNeedsTS();
    m_bIsMarker = true;    
    m_pCurrPktFn = &TrcPktProcStm::stmPktD16;
    (this->*m_pCurrPktFn)();
}

void TrcPktProcStm::stmPktD32MTS()
{
    pktNeedsTS();
    m_bIsMarker = true;    
    m_pCurrPktFn = &TrcPktProcStm::stmPktD32;
    (this->*m_pCurrPktFn)();
}

void TrcPktProcStm::stmPktD64MTS()
{
    pktNeedsTS();
    m_bIsMarker = true;    
    m_pCurrPktFn = &TrcPktProcStm::stmPktD64;
    (this->*m_pCurrPktFn)();
}

void TrcPktProcStm::stmPktFlagTS()
{
    pktNeedsTS();
    m_curr_packet.setPacketType(STM_PKT_FLAG,false);
    m_pCurrPktFn = &TrcPktProcStm::stmExtractTS;
    (this->*m_pCurrPktFn)();
}

void TrcPktProcStm::stmPktFExt()
{
    // no type, look at the next nibble
    if(readNibble())
    {
        // switch in 2N function
        m_pCurrPktFn = m_2N_ops[m_nibble];
        (this->*m_pCurrPktFn)();        
    }
}

// ************************
// 2 nibble opcodes 0xFn
void TrcPktProcStm::stmPktReservedFn()
{
    uint16_t bad_opcode = 0x00F;
    bad_opcode |= ((uint16_t)m_nibble) << 4;
    m_curr_packet.setD16Payload(bad_opcode);
    throwReservedHdrError("STM: Unsupported or Reserved STPv2 Header");
}

void TrcPktProcStm::stmPktF0Ext()
{
    // no type yet, look at the next nibble
    if(readNibble())
    {
        // switch in 3N function
        m_pCurrPktFn = m_3N_ops[m_nibble];
        (this->*m_pCurrPktFn)();        
    }
}

void TrcPktProcStm::stmPktGERR()
{
    if(m_num_nibbles == 2)    // 2nd nibble - header - set type
        m_curr_packet.setPacketType(STM_PKT_GERR,false); 
    stmExtractVal8(4);
    if(m_num_nibbles == 4)
    {
        m_curr_packet.setD8Payload(m_val8);
        m_curr_packet.setMaster(0); // GERR sets current master to 0.
        sendPacket();
    }
}

void TrcPktProcStm::stmPktC16()
{
    if(m_num_nibbles == 2)    // 2nd nibble - header - set type
        m_curr_packet.setPacketType(STM_PKT_C16,false);    
    stmExtractVal16(6);
    if(m_num_nibbles == 6)
    {
        m_curr_packet.setChannel(m_val16,false);
        sendPacket();
    }
}

void TrcPktProcStm::stmPktD4TS()
{
    pktNeedsTS();
    m_curr_packet.setPacketType(STM_PKT_D4,false); // 2nd nibble, set type here
    m_num_data_nibbles = 3; // one more nibble for data
    m_pCurrPktFn = &TrcPktProcStm::stmPktD4;
    (this->*m_pCurrPktFn)();  
}

void TrcPktProcStm::stmPktD8TS()
{
    pktNeedsTS();
    m_curr_packet.setPacketType(STM_PKT_D8,false); // 2nd nibble, set type here
    m_num_data_nibbles = 4;
    m_pCurrPktFn = &TrcPktProcStm::stmPktD8;
    (this->*m_pCurrPktFn)();  
}

void TrcPktProcStm::stmPktD16TS()
{
    pktNeedsTS();
    m_curr_packet.setPacketType(STM_PKT_D16,false); // 2nd nibble, set type here
    m_num_data_nibbles = 6;
    m_pCurrPktFn = &TrcPktProcStm::stmPktD16;
    (this->*m_pCurrPktFn)();  
}

void TrcPktProcStm::stmPktD32TS()
{
    pktNeedsTS();
    m_curr_packet.setPacketType(STM_PKT_D32,false); // 2nd nibble, set type here
    m_num_data_nibbles = 10;
    m_pCurrPktFn = &TrcPktProcStm::stmPktD32;
    (this->*m_pCurrPktFn)();  
}

void TrcPktProcStm::stmPktD64TS()
{
    pktNeedsTS();
    m_curr_packet.setPacketType(STM_PKT_D64,false); // 2nd nibble, set type here
    m_num_data_nibbles = 18;
    m_pCurrPktFn = &TrcPktProcStm::stmPktD64;
    (this->*m_pCurrPktFn)();  
}

void TrcPktProcStm::stmPktD4M()
{
    m_curr_packet.setPacketType(STM_PKT_D4,true); // 2nd nibble, set type here
    m_num_data_nibbles = 3; // one more nibble for data
    m_pCurrPktFn = &TrcPktProcStm::stmPktD4;
    (this->*m_pCurrPktFn)();  
}

void TrcPktProcStm::stmPktD8M()
{
    m_curr_packet.setPacketType(STM_PKT_D8,true); // 2nd nibble, set type here
    m_num_data_nibbles = 4;
    m_pCurrPktFn = &TrcPktProcStm::stmPktD8;
    (this->*m_pCurrPktFn)();  
}

void TrcPktProcStm::stmPktD16M()
{
    m_curr_packet.setPacketType(STM_PKT_D16,true);
    m_num_data_nibbles = 6;
    m_pCurrPktFn = &TrcPktProcStm::stmPktD16;
    (this->*m_pCurrPktFn)();  
}

void TrcPktProcStm::stmPktD32M()
{
    m_curr_packet.setPacketType(STM_PKT_D32,true);
    m_num_data_nibbles = 10;
    m_pCurrPktFn = &TrcPktProcStm::stmPktD32;
    (this->*m_pCurrPktFn)();  
}

void TrcPktProcStm::stmPktD64M()
{
    m_curr_packet.setPacketType(STM_PKT_D64,true);
    m_num_data_nibbles = 18;
    m_pCurrPktFn = &TrcPktProcStm::stmPktD64;
    (this->*m_pCurrPktFn)();  
}

void TrcPktProcStm::stmPktFlag()
{
    m_curr_packet.setPacketType(STM_PKT_FLAG,false);
    sendPacket();
}

// ************************
// 3 nibble opcodes 0xF0n
void TrcPktProcStm::stmPktReservedF0n()
{
    uint16_t bad_opcode = 0x00F;
    bad_opcode |= ((uint16_t)m_nibble) << 8;
    m_curr_packet.setD16Payload(bad_opcode);
    throwReservedHdrError("STM: Unsupported or Reserved STPv2 Header");
}

void TrcPktProcStm::stmPktVersion()
{
    if(m_num_nibbles == 3)
        m_curr_packet.setPacketType(STM_PKT_VERSION,false);

    if(readNibble())
    {
        m_curr_packet.setD8Payload(m_nibble);   // record the version number
        switch(m_nibble)
        {
        case 3:
            m_curr_packet.onVersionPkt(STM_TS_NATBINARY); break;            
        case 4: 
            m_curr_packet.onVersionPkt(STM_TS_GREY); break;
        default:
            // not a version we support.
            throwBadSequenceError("STM VERSION packet : unrecognised version number.");
        }
        sendPacket();
    }
}

void TrcPktProcStm::stmPktTrigger()
{
    if(m_num_nibbles == 3)
        m_curr_packet.setPacketType(STM_PKT_TRIG,false);
    stmExtractVal8(5);
    if(m_num_nibbles == 5)
    {
        m_curr_packet.setD8Payload(m_val8);
        if(m_bNeedsTS)
        {
            m_pCurrPktFn = &TrcPktProcStm::stmExtractTS;
            (this->*m_pCurrPktFn)();
        }
        else
        {
            sendPacket();
        }
    }
}

void TrcPktProcStm::stmPktTriggerTS()
{
    pktNeedsTS();
    m_pCurrPktFn = &TrcPktProcStm::stmPktTrigger;
    (this->*m_pCurrPktFn)(); 
}

void TrcPktProcStm::stmPktFreq()
{
    if(m_num_nibbles == 3)
    {
        m_curr_packet.setPacketType(STM_PKT_FREQ,false);
        m_val32 = 0;
    }
    stmExtractVal32(11);
    if(m_num_nibbles == 11)
    {
        m_curr_packet.setD32Payload(m_val32);
        sendPacket();
    }
}

void TrcPktProcStm::stmPktASync()
{
    // 2 nibbles - 0xFF - must be an async or error.
    bool bCont = true;
    while(bCont)
    {
        bCont = readNibble();
        if(bCont)
        {
            if(m_is_sync) 
            {
                bCont = false;  // stop reading nibbles
                m_bStreamSync = true;   // mark stream in sync
                m_curr_packet.setPacketType(STM_PKT_ASYNC,false);
                clearSyncCount();
                sendPacket();
            }
            else if(!m_sync_start)  // no longer valid sync packet
            {
                throwBadSequenceError("STM: Invalid ASYNC sequence");
            }
        }
    }
}

// ************************
// general data processing

// return false if no more data
// in an STM byte, 3:0 is 1st nibble in protocol order, 7:4 is 2nd nibble.
bool TrcPktProcStm::readNibble()
{
    bool dataFound = true;
    if(m_nibble_2nd_valid)
    {
        m_nibble = m_nibble_2nd;
        m_nibble_2nd_valid = false;
        m_num_nibbles++;
        checkSyncNibble();
    }
    else if(m_data_in_used < m_data_in_size )
    {
        m_nibble = m_p_data_in[m_data_in_used++];
        savePacketByte(m_nibble);
        m_nibble_2nd = (m_nibble >> 4) & 0xF;
        m_nibble_2nd_valid = true;
        m_nibble &= 0xF;
        m_num_nibbles++;
        checkSyncNibble();
    }
    else
        dataFound = false;  // no data available
    return dataFound;
}

void TrcPktProcStm::pktNeedsTS()
{
    m_bNeedsTS = true;
    m_req_ts_nibbles = 0;	
    m_curr_ts_nibbles = 0;
    m_ts_update_value = 0;
    m_ts_req_set = false;
}

void TrcPktProcStm::stmExtractTS()
{
    if(!m_ts_req_set)
    {
        if(readNibble())
        {
            m_req_ts_nibbles = m_nibble;
            if(m_nibble == 0xD) 
                m_req_ts_nibbles = 14;
            else if(m_nibble == 0xE) 
                m_req_ts_nibbles = 16;

            if(m_nibble == 0xF)
                throwBadSequenceError("STM: Invalid timestamp size 0xF"); 
            m_ts_req_set = true;
        }
    }

    if(m_ts_req_set)
    {
        // if we do not have all the nibbles for the TS, get some...
        if(m_req_ts_nibbles != m_curr_ts_nibbles)
        {
            // extract the correct amount of nibbles for the ts value. 
            bool bCont = true;
            while(bCont && (m_curr_ts_nibbles < m_req_ts_nibbles))
            {
                bCont = readNibble();
                if(bCont)
                {
                    m_ts_update_value <<= 4;
                    m_ts_update_value |= m_nibble;
                    m_curr_ts_nibbles++;
                }
            }
        }
       
        // at this point we have the correct amount of nibbles, or have run out of data to process.
        if(m_req_ts_nibbles == m_curr_ts_nibbles)
        {
            uint8_t new_bits = m_req_ts_nibbles * 4;
            if(m_curr_packet.getTSType() == STM_TS_GREY)
            {            
                uint64_t gray_val = bin_to_gray(m_curr_packet.getTSVal());
                if(new_bits == 64)
                {
                    gray_val = m_ts_update_value;
                }
                else
                {
                    uint64_t mask = (0x1ULL << new_bits) - 1;
                    gray_val &= ~mask;
                    gray_val |= m_ts_update_value & mask;
                }
                m_curr_packet.setTS(gray_to_bin(gray_val),new_bits);
            }
            else if(m_curr_packet.getTSType() == STM_TS_NATBINARY)
            {
                m_curr_packet.setTS(m_ts_update_value, new_bits);
            }
            else
                throwBadSequenceError("STM: unknown timestamp encoding");

            sendPacket();
        }
    }
}

// pass in number of nibbles needed to extract the value
void TrcPktProcStm::stmExtractVal8(uint8_t nibbles_to_val)
{
    bool bCont = true;
    while(bCont && (m_num_nibbles < nibbles_to_val))
    {
        bCont = readNibble();
        if(bCont)   // got a nibble
        {
            m_val8 <<= 4;
            m_val8 |= m_nibble;
        }
    }
}

void TrcPktProcStm::stmExtractVal16(uint8_t nibbles_to_val)
{
    bool bCont = true;
    while(bCont && (m_num_nibbles < nibbles_to_val))
    {
        bCont = readNibble();
        if(bCont)   // got a nibble
        {
            m_val16 <<= 4;
            m_val16 |= m_nibble;
        }
    }
}

void TrcPktProcStm::stmExtractVal32(uint8_t nibbles_to_val)
{
    bool bCont = true;
    while(bCont && (m_num_nibbles < nibbles_to_val))
    {
        bCont = readNibble();
        if(bCont)   // got a nibble
        {
            m_val32 <<= 4;
            m_val32 |= m_nibble;
        }
    }
}

void TrcPktProcStm::stmExtractVal64(uint8_t nibbles_to_val)
{
    bool bCont = true;
    while(bCont && (m_num_nibbles < nibbles_to_val))
    {
        bCont = readNibble();
        if(bCont)   // got a nibble
        {
            m_val64 <<= 4;
            m_val64 |= m_nibble;
        }
    }
}

uint64_t TrcPktProcStm::bin_to_gray(uint64_t bin_value)
{
	uint64_t gray_value = 0;
	gray_value = (1ull << 63) & bin_value;
	int i = 62;
	for (; i >= 0; i--) {
		uint64_t gray_arg_1 = ((1ull << (i+1)) & bin_value) >> (i+1);
		uint64_t gray_arg_2 = ((1ull << i) & bin_value) >> i;
		gray_value |= ((gray_arg_1 ^ gray_arg_2) << i);
	}
	return gray_value;
}

uint64_t TrcPktProcStm::gray_to_bin(uint64_t gray_value)
{
	uint64_t bin_value = 0;
	int bin_bit = 0;
	for (; bin_bit < 64; bin_bit++) {
		uint8_t bit_tmp = ((1ull << bin_bit) & gray_value) >> bin_bit;
		uint8_t gray_bit = bin_bit + 1;
		for (; gray_bit < 64; gray_bit++)
			bit_tmp ^= (((1ull << gray_bit) & gray_value) >> gray_bit);

		bin_value |= (bit_tmp << bin_bit);
	}

	return bin_value;
}


void TrcPktProcStm::buildOpTables()
{
    // init all reserved
    for(int i = 0; i < 0x10; i++)
    {
        m_1N_ops[i] = &TrcPktProcStm::stmPktReserved;
        m_2N_ops[i] = &TrcPktProcStm::stmPktReservedFn;
        m_3N_ops[i] = &TrcPktProcStm::stmPktReservedF0n;
    }

    // set the 1N operations
    m_1N_ops[0x0] = &TrcPktProcStm::stmPktNull;
    m_1N_ops[0x1] = &TrcPktProcStm::stmPktM8;
    m_1N_ops[0x2] = &TrcPktProcStm::stmPktMERR;
    m_1N_ops[0x3] = &TrcPktProcStm::stmPktC8;
    m_1N_ops[0x4] = &TrcPktProcStm::stmPktD8;
    m_1N_ops[0x5] = &TrcPktProcStm::stmPktD16;
    m_1N_ops[0x6] = &TrcPktProcStm::stmPktD32;
    m_1N_ops[0x7] = &TrcPktProcStm::stmPktD64;
    m_1N_ops[0x8] = &TrcPktProcStm::stmPktD8MTS;
    m_1N_ops[0x9] = &TrcPktProcStm::stmPktD16MTS;
    m_1N_ops[0xA] = &TrcPktProcStm::stmPktD32MTS;
    m_1N_ops[0xB] = &TrcPktProcStm::stmPktD64MTS;
    m_1N_ops[0xC] = &TrcPktProcStm::stmPktD4;
    m_1N_ops[0xD] = &TrcPktProcStm::stmPktD4MTS;
    m_1N_ops[0xE] = &TrcPktProcStm::stmPktFlagTS;
    m_1N_ops[0xF] = &TrcPktProcStm::stmPktFExt;

    // set the 2N operations 0xFn
    m_2N_ops[0x0] = &TrcPktProcStm::stmPktF0Ext;
    // 0x1 unused in CS STM
    m_2N_ops[0x2] = &TrcPktProcStm::stmPktGERR;
    m_2N_ops[0x3] = &TrcPktProcStm::stmPktC16;
    m_2N_ops[0x4] = &TrcPktProcStm::stmPktD8TS;
    m_2N_ops[0x5] = &TrcPktProcStm::stmPktD16TS;
    m_2N_ops[0x6] = &TrcPktProcStm::stmPktD32TS;
    m_2N_ops[0x7] = &TrcPktProcStm::stmPktD64TS;
    m_2N_ops[0x8] = &TrcPktProcStm::stmPktD8M;
    m_2N_ops[0x9] = &TrcPktProcStm::stmPktD16M;
    m_2N_ops[0xA] = &TrcPktProcStm::stmPktD32M;
    m_2N_ops[0xB] = &TrcPktProcStm::stmPktD64M;
    m_2N_ops[0xC] = &TrcPktProcStm::stmPktD4TS;
    m_2N_ops[0xD] = &TrcPktProcStm::stmPktD4M;
    m_2N_ops[0xE] = &TrcPktProcStm::stmPktFlag;
    m_2N_ops[0xF] = &TrcPktProcStm::stmPktASync;

    // set the 3N operations 0xF0n
    m_3N_ops[0x0] = &TrcPktProcStm::stmPktVersion;
    m_3N_ops[0x1] = &TrcPktProcStm::stmPktNullTS;
    // 0x2 .. 0x5 not used by CS STM
    m_3N_ops[0x6] = &TrcPktProcStm::stmPktTrigger;
    m_3N_ops[0x7] = &TrcPktProcStm::stmPktTriggerTS;
    m_3N_ops[0x8] = &TrcPktProcStm::stmPktFreq;
    // 0x9 .. 0xF not used by CS STM

}

/* End of File trc_pkt_proc_stm.cpp */
