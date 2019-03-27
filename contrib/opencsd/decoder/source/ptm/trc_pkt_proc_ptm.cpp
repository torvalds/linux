/*
 * \file       trc_pkt_proc_ptm.cpp
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

#include "opencsd/ptm/trc_pkt_proc_ptm.h"
#include "opencsd/ptm/trc_cmp_cfg_ptm.h"
#include "common/ocsd_error.h"


#ifdef __GNUC__
// G++ doesn't like the ## pasting
#define PTM_PKTS_NAME "PKTP_PTM"
#else
// VC++ is OK
#define PTM_PKTS_NAME OCSD_CMPNAME_PREFIX_PKTPROC##"_PTM"
#endif

TrcPktProcPtm::TrcPktProcPtm() : TrcPktProcBase(PTM_PKTS_NAME)
{
    InitProcessorState();
    BuildIPacketTable();    
}

TrcPktProcPtm::TrcPktProcPtm(int instIDNum) : TrcPktProcBase(PTM_PKTS_NAME, instIDNum)
{
    InitProcessorState();
    BuildIPacketTable();
}

TrcPktProcPtm::~TrcPktProcPtm()
{

}

ocsd_err_t TrcPktProcPtm::onProtocolConfig()
{
    ocsd_err_t err = OCSD_ERR_NOT_INIT;

    if(m_config != 0)
    {       
        m_chanIDCopy = m_config->getTraceID();        
        err = OCSD_OK;
    }
    return err;
}

ocsd_datapath_resp_t TrcPktProcPtm::processData(  const ocsd_trc_index_t index,
                                                const uint32_t dataBlockSize,
                                                const uint8_t *pDataBlock,
                                                uint32_t *numBytesProcessed)
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    uint8_t currByte = 0;
    
    m_dataInProcessed = 0;
    
    if(!checkInit())
    {
        resp = OCSD_RESP_FATAL_NOT_INIT;
    }
    else
    {
        m_pDataIn = pDataBlock;
        m_dataInLen = dataBlockSize;        
        m_block_idx = index; // index start for current block
    }

    while(  ( ( m_dataInProcessed  < dataBlockSize) || 
              (( m_dataInProcessed  == dataBlockSize) && (m_process_state == SEND_PKT)) ) && 
            OCSD_DATA_RESP_IS_CONT(resp))
    {
        try
        {
            switch(m_process_state)
            {
            case WAIT_SYNC:
                if(!m_waitASyncSOPkt)
                {
                    m_curr_pkt_index = m_block_idx + m_dataInProcessed;
                    m_curr_packet.type = PTM_PKT_NOTSYNC; 
                    m_bAsyncRawOp = hasRawMon();
                }
                resp = waitASync();
                break;

            case PROC_HDR:
                m_curr_pkt_index = m_block_idx + m_dataInProcessed;
                if(readByte(currByte))
                {
                    m_pIPktFn = m_i_table[currByte].pptkFn;
                    m_curr_packet.type = m_i_table[currByte].pkt_type;
                }
                 else
                {
                    // sequencing error - should not get to the point where readByte
                    // fails and m_DataInProcessed  < dataBlockSize
                    // throw data overflow error
                    throw ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_PKT_INTERP_FAIL,m_curr_pkt_index,this->m_chanIDCopy,"Data Buffer Overrun");
                }
                m_process_state = PROC_DATA;

            case PROC_DATA:
                (this->*m_pIPktFn)();                
                break;

            case SEND_PKT:
                resp = outputPacket();
                InitPacketState();
                m_process_state = PROC_HDR;
                break;
            }
        }
        catch(ocsdError &err)
        {
            LogError(err);
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
            const ocsdError &fatal = ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_FAIL,m_curr_pkt_index,m_chanIDCopy,"Unknown System Error decoding trace.");
            LogError(fatal);
        }

    }
    *numBytesProcessed = m_dataInProcessed;
    return resp;
}

ocsd_datapath_resp_t TrcPktProcPtm::onEOT()
{
    ocsd_datapath_resp_t err = OCSD_RESP_FATAL_NOT_INIT;
    if(checkInit())
    {
        err = OCSD_RESP_CONT;
        if(m_currPacketData.size() > 0)
        {
            m_curr_packet.SetErrType(PTM_PKT_INCOMPLETE_EOT);
            err = outputPacket();
        }
    }
    return err;
}

ocsd_datapath_resp_t TrcPktProcPtm::onReset()
{
    ocsd_datapath_resp_t err = OCSD_RESP_FATAL_NOT_INIT;
    if(checkInit())
    {
        InitProcessorState();
        err = OCSD_RESP_CONT;
    }
    return err;
}

ocsd_datapath_resp_t TrcPktProcPtm::onFlush()
{
    ocsd_datapath_resp_t err = OCSD_RESP_FATAL_NOT_INIT;
    if(checkInit())
    {
         err = OCSD_RESP_CONT;
    }
    return err;
}

const bool TrcPktProcPtm::isBadPacket() const
{
    return m_curr_packet.isBadPacket();
}

void TrcPktProcPtm::InitPacketState()
{
    m_curr_packet.Clear();

}

void TrcPktProcPtm::InitProcessorState()
{
    m_curr_packet.SetType(PTM_PKT_NOTSYNC);
    m_pIPktFn = &TrcPktProcPtm::pktReserved;
    m_process_state = WAIT_SYNC;
    m_async_0 = 0;
    m_waitASyncSOPkt = false;
    m_bAsyncRawOp = false;
    m_bOPNotSyncPkt = false;

    m_curr_packet.ResetState();
    InitPacketState();
}

const bool TrcPktProcPtm::readByte(uint8_t &currByte)
{
    bool bValidByte = false;
    
    if(m_dataInProcessed < m_dataInLen)
    {
        currByte = m_pDataIn[m_dataInProcessed++];
        m_currPacketData.push_back(currByte);
        bValidByte = true;
    }
    return bValidByte;
}

void TrcPktProcPtm::unReadByte()
{
    m_dataInProcessed--;
    m_currPacketData.pop_back();
}

ocsd_datapath_resp_t TrcPktProcPtm::outputPacket()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    resp = outputOnAllInterfaces(m_curr_pkt_index,&m_curr_packet,&m_curr_packet.type,m_currPacketData);
    m_currPacketData.clear();
    return resp;
}

/*** sync and packet functions ***/
ocsd_datapath_resp_t TrcPktProcPtm::waitASync()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;

    // looking for possible patterns in input buffer:-
    // a) ASYNC @ start :          00 00 00 00 00 80
    // b) unsync then async:       xx xx xx xx xx xx xx xx 00 00 00 00 00 80
    // c) unsync (may have 00)     xx xx xx xx 00 xx xx 00 00 00 xx xx xx xx 
    // d) unsync then part async:  xx xx xx xx xx xx xx xx xx xx xx 00 00 00
    // e) unsync with prev part async [00 00 00] 00 xx xx xx xx xx xx xx xx  [] = byte in previous input buffer

    // bytes to read before throwing an unsynced packet
    #define UNSYNC_PKT_MAX 16
    static const uint8_t spare_zeros[] = {  0,0,0,0,0,0,0,0, 
                                            0,0,0,0,0,0,0,0 };

    bool doScan = true;
    bool bSendUnsyncedData = false;
    bool bHaveASync = false;
    int unsynced_bytes = 0;
    int unsync_scan_block_start = 0;
    int pktBytesOnEntry = m_currPacketData.size();  // did we have part of a potential async last time?

    while(doScan && OCSD_DATA_RESP_IS_CONT(resp))
    {
        // may have spotted the start of an async
        if(m_waitASyncSOPkt == true)
        {
            switch(findAsync())
            {
            case ASYNC:
            case ASYNC_EXTRA_0:
                m_process_state = SEND_PKT; 
                m_waitASyncSOPkt = false;
                bSendUnsyncedData = true;
                bHaveASync = true;
                doScan = false;
                break;

            case THROW_0:
                // remove a bunch of 0s 
                unsynced_bytes += ASYNC_PAD_0_LIMIT;
                m_waitASyncSOPkt = false;
                m_currPacketData.erase( m_currPacketData.begin(), m_currPacketData.begin()+ASYNC_PAD_0_LIMIT);                
                break;

            case NOT_ASYNC:
                unsynced_bytes += m_currPacketData.size();
                m_waitASyncSOPkt = false;
                m_currPacketData.clear();
                break;

            case ASYNC_INCOMPLETE:
                bSendUnsyncedData = true;
                doScan = false;
                break;
            }
        }
        else 
        {
            if(m_pDataIn[m_dataInProcessed++] == 0x00)
            {
                m_waitASyncSOPkt = true;
                m_currPacketData.push_back(0); 
                m_async_0 = 1;
            }
            else
            {
                unsynced_bytes++;
            }
        }        

        // may need to send some unsynced data here, either if we have enought to make it worthwhile, 
        // or are at the end of the buffer.
        if(unsynced_bytes >= UNSYNC_PKT_MAX) 
            bSendUnsyncedData = true;

        if(m_dataInProcessed == m_dataInLen)
        {
            bSendUnsyncedData = true;
            doScan = false;  // no more data available - stop the scan
        }

        // will send any unsynced data
        if(bSendUnsyncedData && (unsynced_bytes > 0))
        {      
            if(m_bAsyncRawOp)
            {
                // there were some 0's in the packet buyffer from the last pass that are no longer in the raw buffer,
                // and these turned out not to be an async
                if(pktBytesOnEntry)
                {
                    outputRawPacketToMonitor(m_curr_pkt_index,&m_curr_packet,pktBytesOnEntry,spare_zeros);
                    m_curr_pkt_index += pktBytesOnEntry;
                }
                outputRawPacketToMonitor(m_curr_pkt_index,&m_curr_packet,unsynced_bytes,m_pDataIn+unsync_scan_block_start);
            }
            if (!m_bOPNotSyncPkt)
            {
                resp = outputDecodedPacket(m_curr_pkt_index, &m_curr_packet);
                m_bOPNotSyncPkt = true;
            }
            unsync_scan_block_start += unsynced_bytes;
            m_curr_pkt_index+= unsynced_bytes;
            unsynced_bytes = 0;
            bSendUnsyncedData = false;
        }
        
        // mark next packet as the ASYNC we are looking for.
        if(bHaveASync)
            m_curr_packet.SetType(PTM_PKT_A_SYNC);       
    }

    return resp; 
}

void TrcPktProcPtm::pktASync()
{
    if(m_currPacketData.size() == 1) // header byte
    {
        m_async_0 = 1;
    }

    switch(findAsync())
    {
    case ASYNC:
    case ASYNC_EXTRA_0:
        m_process_state = SEND_PKT; 
        break;

    case THROW_0:
    case NOT_ASYNC:
        throwMalformedPacketErr("Bad Async packet");
        break;

    case ASYNC_INCOMPLETE:
        break;
    
    }
}

TrcPktProcPtm::async_result_t TrcPktProcPtm::findAsync()
{
    async_result_t async_res = NOT_ASYNC;
    bool bFound = false; // found non-zero byte in sequence
    bool bByteAvail = true;
    uint8_t currByte;
    
    while(!bFound && bByteAvail)
    {
        if(readByte(currByte))
        {
            if(currByte == 0x00)
            {
                m_async_0++;
                if(m_async_0 >= (ASYNC_PAD_0_LIMIT + ASYNC_REQ_0))
                {
                    bFound = true;
                    async_res = THROW_0;
                }
            }
            else
            {
                if(currByte == 0x80)
                {
                    if(m_async_0 == 5)
                        async_res = ASYNC;
                    else if(m_async_0 > 5)
                        async_res = ASYNC_EXTRA_0;
                }
                bFound = true;
            }
        }
        else
        {
            bByteAvail = false;
            async_res = ASYNC_INCOMPLETE;
        }
    }
    return async_res;
}

void TrcPktProcPtm::pktISync()
{
    uint8_t currByte = 0;
    int pktIndex = m_currPacketData.size() - 1;
    bool bGotBytes = false, validByte = true;

    if(pktIndex == 0)
    {
        m_numCtxtIDBytes = m_config->CtxtIDBytes();
        m_gotCtxtIDBytes = 0;

        // total bytes = 6 + ctxtID; (perhaps more later)
        m_numPktBytesReq = 6 + m_numCtxtIDBytes;
    }

    while(validByte && !bGotBytes)
    {
        if(readByte(currByte))
        {           
            pktIndex = m_currPacketData.size() - 1;
            if(pktIndex == 5)
            {
                // got the info byte  
                int altISA = (currByte >> 2) & 0x1;
                int reason = (currByte >> 5) & 0x3;
                m_curr_packet.SetISyncReason((ocsd_iSync_reason)(reason));
                m_curr_packet.UpdateNS((currByte >> 3) & 0x1);
                m_curr_packet.UpdateAltISA((currByte >> 2) & 0x1);
                m_curr_packet.UpdateHyp((currByte >> 1) & 0x1);

                ocsd_isa isa = ocsd_isa_arm;
                if(m_currPacketData[1] & 0x1)
                    isa = altISA ? ocsd_isa_tee : ocsd_isa_thumb2;
                m_curr_packet.UpdateISA(isa);

                // check cycle count required - not if reason == 0;
                m_needCycleCount = (reason != 0) ? m_config->enaCycleAcc() : false;
                m_gotCycleCount = false;
                m_numPktBytesReq += (m_needCycleCount ? 1 : 0);
                m_gotCCBytes = 0;

            }
            else if(pktIndex > 5)
            {
                // cycle count appears first if present
                if(m_needCycleCount && !m_gotCycleCount)
                {
                    if(pktIndex == 6)
                        m_gotCycleCount = (bool)((currByte & 0x40) == 0);   // no cont bit, got cycle count
                    else
                        m_gotCycleCount = ((currByte & 0x80) == 0) || (pktIndex == 10);

                    m_gotCCBytes++;     // count the cycle count bytes for later use.
                    if(!m_gotCycleCount)    // need more cycle count bytes
                        m_numPktBytesReq++;
                }
                // then context ID if present.
                else if( m_numCtxtIDBytes > m_gotCtxtIDBytes)
                {
                    m_gotCtxtIDBytes++;
                }
            }

            // check if we have enough bytes
            bGotBytes = (bool)((unsigned)m_numPktBytesReq == m_currPacketData.size());
        }
        else 
            validByte = false;  // no byte available, exit.
    }

    if(bGotBytes)
    {
        // extract address value, cycle count and ctxt id.
        uint32_t cycleCount = 0;
        uint32_t ctxtID = 0;
        int optIdx = 6; // start index for optional elements.

        // address is always full fixed 32 bit value
        uint32_t address = ((uint32_t)m_currPacketData[1]) & 0xFE;
        address |= ((uint32_t)m_currPacketData[2]) << 8;
        address |= ((uint32_t)m_currPacketData[3]) << 16;
        address |= ((uint32_t)m_currPacketData[4]) << 24;
        m_curr_packet.UpdateAddress(address,32);

        if(m_needCycleCount)
        {
            extractCycleCount(optIdx,cycleCount);
            m_curr_packet.SetCycleCount(cycleCount);
            optIdx+=m_gotCCBytes;
        }

        if(m_numCtxtIDBytes)
        {
            extractCtxtID(optIdx,ctxtID);
            m_curr_packet.UpdateContextID(ctxtID);
        }
        m_process_state = SEND_PKT;
    }
}

void TrcPktProcPtm::pktTrigger()
{
    m_process_state = SEND_PKT;    // no payload
}

void TrcPktProcPtm::pktWPointUpdate()
{
    bool bDone = false;
    bool bBytesAvail = true;
    uint8_t currByte = 0;
    int byteIdx = 0;

    if(m_currPacketData.size() == 1)
    {        
        m_gotAddrBytes = false;    // flag to indicate got all needed address bytes
        m_numAddrBytes = 0;        // number of address bytes so far - in this case header is not part of the address
               
        m_gotExcepBytes = false;    // mark as not got all required exception bytes thus far
        m_numExcepBytes = 0;        // 0 read in

         m_addrPktIsa = ocsd_isa_unknown; // not set by this packet as yet        
    }

    // collect all the bytes needed
    while(!bDone && bBytesAvail)
    {
        if(readByte(currByte))
        {
            byteIdx = m_currPacketData.size() - 1;
            if(!m_gotAddrBytes)
            {
                if(byteIdx < 4)
                {
                    // address bytes  1 - 4;
                    // ISA stays the same
                    if((currByte & 0x80) == 0x00)
                    {
                        // no further bytes
                        m_gotAddrBytes = true;
                        bDone = true;
                        m_gotExcepBytes = true;
                    }
                }
                else
                {
                    // 5th address byte - determine ISA from this.
                    if((currByte & 0x40) == 0x00)
                        m_gotExcepBytes = true; // no exception bytes - mark as done
                    m_gotAddrBytes = true;
                    bDone = m_gotExcepBytes;

                    m_addrPktIsa = ocsd_isa_arm;   // assume ARM, but then check
                    if((currByte & 0x20) == 0x20)   // bit 5 == 1'b1 - jazelle, bits 4 & 3 part of address.
                        m_addrPktIsa = ocsd_isa_jazelle;
                    else if((currByte & 0x30) == 0x10) // bit [5:4] == 2'b01 - thumb, bit 3 part of address.
                        m_addrPktIsa = ocsd_isa_thumb2;                       
                } 
                m_numAddrBytes++;
            }
            else if(!m_gotExcepBytes)
            {
                // excep byte is actually a WP update byte.
                m_excepAltISA = ((currByte & 0x40) == 0x40) ? 1 : 0;
                m_gotExcepBytes = true;
                m_numExcepBytes++;
                bDone = true;
            }
        }
        else
            bBytesAvail = false;
    }

    // analyse the bytes to create the packet
    if(bDone)
    {
        // ISA for the packet
        if(m_addrPktIsa == ocsd_isa_unknown) // unchanged by trace packet
            m_addrPktIsa = m_curr_packet.getISA(); // same as prev

        if(m_gotExcepBytes) // may adjust according to alt ISA in exception packet
        {
            if((m_addrPktIsa == ocsd_isa_tee)  && (m_excepAltISA == 0))
                m_addrPktIsa = ocsd_isa_thumb2;
            else if((m_addrPktIsa == ocsd_isa_thumb2) && (m_excepAltISA == 1))
                m_addrPktIsa = ocsd_isa_tee;
        }
        m_curr_packet.UpdateISA(m_addrPktIsa); // mark ISA in packet (update changes current and prev to dectect an ISA change).

        uint8_t total_bits = 0;
        uint32_t addr_val = extractAddress(1,total_bits);
        m_curr_packet.UpdateAddress(addr_val,total_bits);
        m_process_state = SEND_PKT;
    }
}

void TrcPktProcPtm::pktIgnore()
{
    m_process_state = SEND_PKT;    // no payload
}

void TrcPktProcPtm::pktCtxtID()
{
    int pktIndex = m_currPacketData.size() - 1;

    // if at the header, determine how many more bytes we need.
    if(pktIndex == 0)
    {
        m_numCtxtIDBytes = m_config->CtxtIDBytes();
        m_gotCtxtIDBytes = 0;
    }

    // read the necessary ctxtID bytes from the stream
    bool bGotBytes = false, bytesAvail = true;
    uint32_t ctxtID = 0;

    bGotBytes = m_numCtxtIDBytes == m_gotCtxtIDBytes;
    while(!bGotBytes & bytesAvail)
    {
        bytesAvail = readByte();
        if(bytesAvail)
            m_gotCtxtIDBytes++;
        bGotBytes = m_numCtxtIDBytes == m_gotCtxtIDBytes;
    }

    if(bGotBytes)
    {
        if(m_numCtxtIDBytes)
        {
            extractCtxtID(1,ctxtID);
        }
        m_curr_packet.UpdateContextID(ctxtID);
        m_process_state = SEND_PKT;
    }
}

void TrcPktProcPtm::pktVMID()
{
    uint8_t currByte;
    
    // just need a single payload byte...
    if(readByte(currByte))
    {
        m_curr_packet.UpdateVMID(currByte);
        m_process_state = SEND_PKT;
    }
}

void TrcPktProcPtm::pktAtom()
{
    uint8_t pHdr = m_currPacketData[0];

    if(!m_config->enaCycleAcc())    
    {
        m_curr_packet.SetAtomFromPHdr(pHdr);
        m_process_state = SEND_PKT;
    }
    else
    {
        bool bGotAllPktBytes = false, byteAvail = true;
        uint8_t currByte = 0;        // cycle accurate tracing -> atom + cycle count       

        if(!(pHdr & 0x40))
        {
            // only the header byte present
            bGotAllPktBytes = true;
        }
        else 
        {
            // up to 4 additional bytes of count data.
            while(byteAvail && !bGotAllPktBytes)
            {
                if(readByte(currByte))
                {
                    if(!(currByte & 0x80) || (m_currPacketData.size() == 5))
                        bGotAllPktBytes = true;
                }
                else
                    byteAvail = false;
            }
        }

        // we have all the bytes for a cycle accurate packet.
        if(bGotAllPktBytes)
        {
            uint32_t cycleCount = 0;
            extractCycleCount(0,cycleCount);
            m_curr_packet.SetCycleCount(cycleCount);
            m_curr_packet.SetCycleAccAtomFromPHdr(pHdr);
            m_process_state = SEND_PKT;
        }
    }
}

void TrcPktProcPtm::pktTimeStamp()
{
    uint8_t currByte = 0;
    int pktIndex = m_currPacketData.size() - 1;
    bool bGotBytes = false, byteAvail = true;

    if(pktIndex == 0)
    {
        m_gotTSBytes = false;
        m_needCycleCount = m_config->enaCycleAcc();        
        m_gotCCBytes = 0;

        // max byte buffer size for full ts packet
        m_tsByteMax = m_config->TSPkt64() ? 10 : 8;
    }

    while(byteAvail && !bGotBytes)
    {
        if(readByte(currByte))
        {
            if(!m_gotTSBytes)
            {
                if(((currByte & 0x80) == 0) || (m_currPacketData.size() == (unsigned)m_tsByteMax))
                {
                    m_gotTSBytes = true;
                    if(!m_needCycleCount)
                        bGotBytes = true;
                }
            }
            else
            {
                uint8_t cc_cont_mask = 0x80;
                // got TS bytes, collect cycle count
                if(m_gotCCBytes == 0)
                    cc_cont_mask = 0x40;
                if((currByte & cc_cont_mask) == 0)
                    bGotBytes = true;
                m_gotCCBytes++;
                if(m_gotCCBytes == 5)
                    bGotBytes = true;
            }
        }
        else
            byteAvail = false;
    }

    if(bGotBytes)
    {
        uint64_t tsVal = 0;
        uint32_t cycleCount = 0;
        uint8_t tsUpdateBits = 0;
        int ts_end_idx = extractTS(tsVal,tsUpdateBits);
        if(m_needCycleCount)
        {
            extractCycleCount(ts_end_idx,cycleCount);
            m_curr_packet.SetCycleCount(cycleCount);
        }
        m_curr_packet.UpdateTimestamp(tsVal,tsUpdateBits); 
        m_process_state = SEND_PKT;
    }
}

void TrcPktProcPtm::pktExceptionRet()
{
     m_process_state = SEND_PKT;    // no payload
}

void TrcPktProcPtm::pktBranchAddr()
{
    uint8_t currByte = m_currPacketData[0];
    bool bDone = false;
    bool bBytesAvail = true;
    int byteIdx = 0;

    if(m_currPacketData.size() == 1)
    {        
        m_gotAddrBytes = false;    // flag to indicate got all needed address bytes
        m_numAddrBytes = 1;        // number of address bytes so far
        
        m_needCycleCount = m_config->enaCycleAcc();  // check if we have a cycle count
        m_gotCCBytes = 0;                            // number of cc bytes read in so far.
        
        m_gotExcepBytes = false;    // mark as not got all required exception bytes thus far
        m_numExcepBytes = 0;        // 0 read in
        
        m_addrPktIsa = ocsd_isa_unknown; // not set by this packet as yet
        
        // header is also 1st address byte
        if((currByte & 0x80) == 0)  // could be single byte packet
        {
            m_gotAddrBytes = true;
            if(!m_needCycleCount)
                bDone = true;   // all done if no cycle count
            m_gotExcepBytes = true; // cannot have exception bytes following single byte packet
        }
        
    }

    // collect all the bytes needed
    while(!bDone && bBytesAvail)
    {
        if(readByte(currByte))
        {
            byteIdx = m_currPacketData.size() - 1;
            if(!m_gotAddrBytes)
            {
                if(byteIdx < 4)
                {
                    // address bytes  2 - 4;
                    // ISA stays the same
                    if((currByte & 0x80) == 0x00)
                    {
                        // no further bytes
                        if((currByte & 0x40) == 0x00)
                            m_gotExcepBytes = true; // no exception bytes - mark as done
                        m_gotAddrBytes = true;
                        bDone = m_gotExcepBytes && !m_needCycleCount;
                    }
                }
                else
                {
                    // 5th address byte - determine ISA from this.
                    if((currByte & 0x40) == 0x00)
                        m_gotExcepBytes = true; // no exception bytes - mark as done
                    m_gotAddrBytes = true;
                    bDone = m_gotExcepBytes && !m_needCycleCount;

                    m_addrPktIsa = ocsd_isa_arm;   // assume ARM, but then check
                    if((currByte & 0x20) == 0x20)   // bit 5 == 1'b1 - jazelle, bits 4 & 3 part of address.
                        m_addrPktIsa = ocsd_isa_jazelle;
                    else if((currByte & 0x30) == 0x10) // bit [5:4] == 2'b01 - thumb, bit 3 part of address.
                        m_addrPktIsa = ocsd_isa_thumb2;                       
                } 
                m_numAddrBytes++;
            }
            else if(!m_gotExcepBytes)
            {
                // may need exception bytes
                if(m_numExcepBytes == 0)
                {
                    if((currByte & 0x80) == 0x00)
                        m_gotExcepBytes = true;
                     m_excepAltISA = ((currByte & 0x40) == 0x40) ? 1 : 0;
                }
                else
                    m_gotExcepBytes = true;
                m_numExcepBytes++;

                if(m_gotExcepBytes && !m_needCycleCount)
                    bDone = true;

            }
            else if(m_needCycleCount)
            {
                // not done after exception bytes, collect cycle count
                if(m_gotCCBytes == 0)
                {
                    bDone = ((currByte & 0x40) == 0x00 );
                }
                else
                {
                    // done if no more or 5th byte
                    bDone = (((currByte & 0x80) == 0x00 ) || (m_gotCCBytes == 4));
                }
                m_gotCCBytes++;
            }
            else
                // this should never be reached.
                throwMalformedPacketErr("sequencing error analysing branch packet");
        }
        else
            bBytesAvail = false;
    }

    // analyse the bytes to create the packet
    if(bDone)
    {
        // ISA for the packet
        if(m_addrPktIsa == ocsd_isa_unknown) // unchanged by trace packet
            m_addrPktIsa = m_curr_packet.getISA(); // same as prev

        if(m_gotExcepBytes) // may adjust according to alt ISA in exception packet
        {
            if((m_addrPktIsa == ocsd_isa_tee)  && (m_excepAltISA == 0))
                m_addrPktIsa = ocsd_isa_thumb2;
            else if((m_addrPktIsa == ocsd_isa_thumb2) && (m_excepAltISA == 1))
                m_addrPktIsa = ocsd_isa_tee;
        }
        m_curr_packet.UpdateISA(m_addrPktIsa); // mark ISA in packet (update changes current and prev to dectect an ISA change).



        // we know the ISA, we can correctly interpret the address.   
        uint8_t total_bits = 0;
        uint32_t addr_val = extractAddress(0,total_bits);
        m_curr_packet.UpdateAddress(addr_val,total_bits);

        if(m_numExcepBytes > 0)
        {
            uint8_t E1 = m_currPacketData[m_numAddrBytes];
            uint16_t ENum = (E1 >> 1) & 0xF;
            ocsd_armv7_exception excep = Excp_Reserved;

            m_curr_packet.UpdateNS(E1 & 0x1);
            if(m_numExcepBytes > 1)
            {
                uint8_t E2 = m_currPacketData[m_numAddrBytes+1];
                m_curr_packet.UpdateHyp((E2 >> 5) & 0x1);
                ENum |= ((uint16_t)(E2 & 0x1F) << 4);
            }

            if(ENum <= 0xF)
            {
                static ocsd_armv7_exception v7ARExceptions[16] = {
                    Excp_NoException, Excp_DebugHalt, Excp_SMC, Excp_Hyp,
                    Excp_AsyncDAbort, Excp_ThumbEECheckFail, Excp_Reserved, Excp_Reserved,
                    Excp_Reset, Excp_Undef, Excp_SVC, Excp_PrefAbort,
                    Excp_SyncDataAbort, Excp_Generic, Excp_IRQ, Excp_FIQ
                };
                excep = v7ARExceptions[ENum];
            }
            m_curr_packet.SetException(excep,ENum);
        }

        if(m_needCycleCount)
        {
            int countIdx = m_numAddrBytes + m_numExcepBytes;
            uint32_t cycleCount = 0;
            extractCycleCount(countIdx,cycleCount);
            m_curr_packet.SetCycleCount(cycleCount);
        }
        m_process_state = SEND_PKT;
    }
}

void TrcPktProcPtm::pktReserved()
{
     m_process_state = SEND_PKT;    // no payload
}

void TrcPktProcPtm::extractCtxtID(int idx, uint32_t &ctxtID)
{
    ctxtID = 0;
    int shift = 0;
    for(int i=0; i < m_numCtxtIDBytes; i++)
    {
        if((size_t)idx+i >= m_currPacketData.size())
            throwMalformedPacketErr("Insufficient packet bytes for Context ID value.");
        ctxtID |= ((uint32_t)m_currPacketData[idx+i]) << shift;
        shift+=8;
    }
}

void TrcPktProcPtm::extractCycleCount(int offset, uint32_t &cycleCount)
{
    bool bCont = true;
    cycleCount = 0;
    int by_idx = 0;
    uint8_t currByte;
    int shift = 4;

    while(bCont)
    {
        if((size_t)by_idx+offset >= m_currPacketData.size())
            throwMalformedPacketErr("Insufficient packet bytes for Cycle Count value.");

        currByte = m_currPacketData[offset+by_idx];
        if(by_idx == 0)
        {
            bCont = (currByte & 0x40) != 0;
            cycleCount = (currByte >> 2) & 0xF;
        }
        else
        {
                    
            bCont = (currByte & 0x80) != 0;
            if(by_idx == 4)
                bCont = false;
            cycleCount |= (((uint32_t)(currByte & 0x7F)) << shift);
            shift += 7;
        }
        by_idx++;
    }
}

int TrcPktProcPtm::extractTS(uint64_t &tsVal,uint8_t &tsUpdateBits)
{
    bool bCont = true;
    int tsIdx = 1;  // start index;
    uint8_t byteVal;
    bool b64BitVal = m_config->TSPkt64();
    int shift = 0;

    tsVal = 0;
    tsUpdateBits = 0;

    while(bCont)
    {
        if((size_t)tsIdx >= m_currPacketData.size())
            throwMalformedPacketErr("Insufficient packet bytes for Timestamp value.");
        
        byteVal = m_currPacketData[tsIdx];
       
        if(b64BitVal)
        {
            if(tsIdx < 9)
            {
                bCont = ((byteVal & 0x80) == 0x80);
                byteVal &= 0x7F;
                tsUpdateBits += 7;
            }
            else
            {
                bCont = false;
                tsUpdateBits += 8;
            }
        }
        else
        {
            if(tsIdx < 7)
            {
                bCont = ((byteVal & 0x80) == 0x80);
                byteVal &= 0x7F;
                tsUpdateBits += 7;
            }
            else
            {
                byteVal &=0x3F;
                bCont = false;
                tsUpdateBits += 6;
            }
        }
        tsVal |= (((uint64_t)byteVal) << shift);
        tsIdx++;
        shift += 7;
    }
    return tsIdx;   // return next byte index in packet.
}

uint32_t TrcPktProcPtm::extractAddress(const int offset, uint8_t &total_bits)
{
    // we know the ISA, we can correctly interpret the address.   
    uint32_t addr_val = 0;
    uint8_t mask = 0x7E;    // first byte mask (always);
    uint8_t num_bits = 0x7; // number of bits in the 1st byte (thumb);
    int shift = 0;
    int next_shift = 0;

    total_bits = 0;

    for(int i = 0; i < m_numAddrBytes; i++)
    {
        if(i == 4)
        {
            // 5th byte mask
            mask = 0x0f;    // thumb mask;
            num_bits = 4;
            if(m_addrPktIsa == ocsd_isa_jazelle)
            {
                mask = 0x1F;
                num_bits = 5;
            }
            else if(m_addrPktIsa == ocsd_isa_arm)
            {
                mask = 0x07;
                num_bits = 3;
            }
        }
        else if(i > 0)
        {
            mask = 0x7F;
            num_bits = 7;
            // check for last byte but not 1st or 5th byte mask
            if(i == m_numAddrBytes-1)
            {
                mask = 0x3F;
                num_bits = 6;
            }
        }

        // extract data
        shift = next_shift;
        addr_val |= ((uint32_t)(m_currPacketData[i+offset] & mask) << shift);
        total_bits += num_bits;

        // how much we shift the next value
        if(i == 0)
        {
            if(m_addrPktIsa == ocsd_isa_jazelle)
            {
                addr_val >>= 1;
                next_shift = 6;
                total_bits--;   // adjust bits for jazelle offset
            }
            else
            {
                next_shift = 7;
            }
        }
        else 
        {
            next_shift += 7;
        }
    }

    if(m_addrPktIsa == ocsd_isa_arm)
    {
        addr_val <<= 1; // shift one extra bit for ARM address alignment.
        total_bits++;
    }
    return addr_val;
}


void TrcPktProcPtm::BuildIPacketTable()
{
    // initialise all to branch, atom or reserved packet header
    for(unsigned i = 0; i < 256; i++)
    {
        // branch address packets all end in 8'bxxxxxxx1
        if((i & 0x01) == 0x01)
        {
            m_i_table[i].pkt_type = PTM_PKT_BRANCH_ADDRESS;
            m_i_table[i].pptkFn = &TrcPktProcPtm::pktBranchAddr;
        }
        // atom packets are 8'b1xxxxxx0
        else if((i & 0x81) == 0x80)
        {
            m_i_table[i].pkt_type = PTM_PKT_ATOM;
            m_i_table[i].pptkFn = &TrcPktProcPtm::pktAtom;
        }
        else
        {
            // set all the others to reserved for now
            m_i_table[i].pkt_type = PTM_PKT_RESERVED;
            m_i_table[i].pptkFn = &TrcPktProcPtm::pktReserved;
        }
    }

    // pick out the other packet types by individual codes.

    // A-sync           8'b00000000
    m_i_table[0x00].pkt_type = PTM_PKT_A_SYNC;
    m_i_table[0x00].pptkFn = &TrcPktProcPtm::pktASync;

    // I-sync           8'b00001000
    m_i_table[0x08].pkt_type = PTM_PKT_I_SYNC;
    m_i_table[0x08].pptkFn = &TrcPktProcPtm::pktISync;

    // waypoint update  8'b01110010
    m_i_table[0x72].pkt_type = PTM_PKT_WPOINT_UPDATE;
    m_i_table[0x72].pptkFn = &TrcPktProcPtm::pktWPointUpdate;
    
    // trigger          8'b00001100
    m_i_table[0x0C].pkt_type = PTM_PKT_TRIGGER;
    m_i_table[0x0C].pptkFn = &TrcPktProcPtm::pktTrigger;

    // context ID       8'b01101110
    m_i_table[0x6E].pkt_type = PTM_PKT_CONTEXT_ID;
    m_i_table[0x6E].pptkFn = &TrcPktProcPtm::pktCtxtID;

    // VMID             8'b00111100
    m_i_table[0x3C].pkt_type = PTM_PKT_VMID;
    m_i_table[0x3C].pptkFn = &TrcPktProcPtm::pktVMID;

    // Timestamp        8'b01000x10
    m_i_table[0x42].pkt_type = PTM_PKT_TIMESTAMP;
    m_i_table[0x42].pptkFn = &TrcPktProcPtm::pktTimeStamp;
    m_i_table[0x46].pkt_type = PTM_PKT_TIMESTAMP;
    m_i_table[0x46].pptkFn = &TrcPktProcPtm::pktTimeStamp;

    // Exception return 8'b01110110
    m_i_table[0x76].pkt_type = PTM_PKT_EXCEPTION_RET;
    m_i_table[0x76].pptkFn = &TrcPktProcPtm::pktExceptionRet;

    // Ignore           8'b01100110
    m_i_table[0x66].pkt_type = PTM_PKT_IGNORE;
    m_i_table[0x66].pptkFn = &TrcPktProcPtm::pktIgnore;
}

/* End of File trc_pkt_proc_ptm.cpp */
