/*
 * \file       trc_pkt_proc_etmv3_impl.cpp
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

#include "trc_pkt_proc_etmv3_impl.h"

EtmV3PktProcImpl::EtmV3PktProcImpl() :
    m_isInit(false),
    m_interface(0)
{
}

EtmV3PktProcImpl::~EtmV3PktProcImpl()
{
}

ocsd_err_t EtmV3PktProcImpl::Configure(const EtmV3Config *p_config)
{
    ocsd_err_t err = OCSD_OK;
    if(p_config != 0)
    {
        m_config = *p_config;
        m_chanIDCopy = m_config.getTraceID();
    }
    else
    {
        err = OCSD_ERR_INVALID_PARAM_VAL;
        if(m_isInit)
            m_interface->LogError(ocsdError(OCSD_ERR_SEV_ERROR,err));
    }
    return err;
}

ocsd_datapath_resp_t EtmV3PktProcImpl::processData(const ocsd_trc_index_t index,
                                                    const uint32_t dataBlockSize,
                                                    const uint8_t *pDataBlock,
                                                    uint32_t *numBytesProcessed)
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    m_bytesProcessed = 0;

    while( ( (m_bytesProcessed < dataBlockSize) ||
             ((m_bytesProcessed == dataBlockSize) && (m_process_state == SEND_PKT)) )
        && OCSD_DATA_RESP_IS_CONT(resp))
    {
        try 
        {
            switch(m_process_state)
            {
            case WAIT_SYNC:
                if(!m_bStartOfSync)
                    m_packet_index = index +  m_bytesProcessed;
                m_bytesProcessed += waitForSync(dataBlockSize-m_bytesProcessed,pDataBlock+m_bytesProcessed);
                break;

            case PROC_HDR:
                m_packet_index = index +  m_bytesProcessed;
                processHeaderByte(pDataBlock[m_bytesProcessed++]);
                break;

            case PROC_DATA:
                processPayloadByte(pDataBlock [m_bytesProcessed++]);
                break;

            case SEND_PKT:
                resp =  outputPacket();
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
            ocsdError fatal = ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_FAIL,m_packet_index,m_chanIDCopy);
            fatal.setMessage("Unknown System Error decoding trace.");
            m_interface->LogError(fatal);
        }
    }

    *numBytesProcessed = m_bytesProcessed;
    return resp;
}

ocsd_datapath_resp_t EtmV3PktProcImpl::onEOT()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    // if we have a partial packet then send to attached sinks
    if(m_currPacketData.size() != 0)
    {
        // TBD: m_curr_packet.updateErrType(ETM4_ETM3_PKT_I_INCOMPLETE_EOT);
        resp = outputPacket();
        InitPacketState();
    }
    return resp;
}

ocsd_datapath_resp_t EtmV3PktProcImpl::onReset()
{
    InitProcessorState();
    return OCSD_RESP_CONT;
}

ocsd_datapath_resp_t EtmV3PktProcImpl::onFlush()
{
    // packet processor never holds on to flushable data (may have partial packet, 
    // but any full packets are immediately sent)
    return OCSD_RESP_CONT;
}

void EtmV3PktProcImpl::Initialise(TrcPktProcEtmV3 *p_interface)
{
    if(p_interface)
    {
        m_interface = p_interface;
        m_isInit = true;
        
    }
    InitProcessorState();
    /* not using pattern matcher for sync at present
    static const uint8_t a_sync[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x80 };
    m_syncMatch.setPattern(a_sync, sizeof(a_sync));*/
}

void EtmV3PktProcImpl::InitProcessorState()
{
    m_bStreamSync = false;  // not synced 
    m_process_state = WAIT_SYNC; // waiting for sync
    m_bStartOfSync = false; // not seen start of sync packet
    m_curr_packet.ResetState(); // reset intra packet state
    InitPacketState();  // set curr packet state
    m_bSendPartPkt = false;
}

void EtmV3PktProcImpl::InitPacketState()
{
    m_bytesExpectedThisPkt = 0; 
	m_BranchPktNeedsException = false;
	m_bIsync_got_cycle_cnt = false;
	m_bIsync_get_LSiP_addr = false;
	m_IsyncInfoIdx = false;
	m_bExpectingDataAddress = false;
	m_bFoundDataAddress = false;
    m_currPacketData.clear();
    m_currPktIdx = 0;       // index into processed bytes in current packet
    m_curr_packet.Clear();
    
}

ocsd_datapath_resp_t EtmV3PktProcImpl::outputPacket()
{
    ocsd_datapath_resp_t dp_resp = OCSD_RESP_FATAL_NOT_INIT;
    if(m_isInit)
    {
        ocsd_etmv3_pkt_type type = m_curr_packet.getType();
        if(!m_bSendPartPkt) 
        {
            dp_resp = m_interface->outputOnAllInterfaces(m_packet_index,&m_curr_packet,&type,m_currPacketData);
            m_process_state = m_bStreamSync ? PROC_HDR : WAIT_SYNC; // need a header next time, or still waiting to sync.
            m_currPacketData.clear();
        }
        else
        {
            // sending part packet, still some data in the main packet
            dp_resp = m_interface->outputOnAllInterfaces(m_packet_index,&m_curr_packet,&type,m_partPktData);
            m_process_state = m_post_part_pkt_state;
            m_packet_index += m_partPktData.size();
            m_bSendPartPkt = false;
            m_curr_packet.SetType(m_post_part_pkt_type);
        }
    }
    return dp_resp;
}

void EtmV3PktProcImpl::setBytesPartPkt(int numBytes, process_state nextState, const ocsd_etmv3_pkt_type nextType)
{
    m_partPktData.clear();
    for(int i=0; i < numBytes; i++)
    {
        m_partPktData.push_back(m_currPacketData[i]);
    }
    m_currPacketData.erase(m_currPacketData.begin(), m_currPacketData.begin()+numBytes);
    m_bSendPartPkt = true;
    m_post_part_pkt_state = nextState;
    m_post_part_pkt_type = nextType;
}

uint32_t EtmV3PktProcImpl::waitForSync(const uint32_t dataBlockSize, const uint8_t *pDataBlock)
{
    uint8_t currByte;
    uint32_t bytesProcessed = 0;
    bool bSendBlock = false;

    // need to wait for the first sync packet
    while(!bSendBlock && (bytesProcessed < dataBlockSize))
    {
        currByte = pDataBlock[bytesProcessed++];
        // TBD: forced sync point

        if(m_bStartOfSync)
        {
            // need to handle consecutive 0 bytes followed by genuine A-SYNC.

            m_currPacketData.push_back(currByte);
            if((currByte == 0x80) && (m_currPacketData.size() >= 6))
            {
                // it is a sync packet possibly with leading zeros
                bSendBlock = true;
                if(m_currPacketData.size() > 6)
                {
                    m_currPacketData.pop_back();
                    bytesProcessed--;   // return 0x80 to the input buffer to re-process next pass after stripping 0's
                    setBytesPartPkt(m_currPacketData.size()-5,WAIT_SYNC,ETM3_PKT_NOTSYNC);
                }
                else
                {
                    m_bStreamSync = true;
                    m_curr_packet.SetType(ETM3_PKT_A_SYNC); 
                }
            }
            else if(currByte != 0x00)
            {
                m_bStartOfSync = false; // not a sync packet                
            }
            else if(m_currPacketData.size() >= 13)  // 13 0's, strip 8 of them...
            {
                setBytesPartPkt(8,WAIT_SYNC,ETM3_PKT_NOTSYNC);
                bSendBlock = true;
            }
        }
        else    // not seen a start of sync candidate yet
        {
            if(currByte == 0x00)  // could be the start of a-sync
            {
                if(m_currPacketData.size() == 0)
                {
                    m_currPacketData.push_back(currByte);
                    m_bStartOfSync = true;
                }
                else
                {
                    bytesProcessed--;
                    bSendBlock = true;  // send none sync packet data, re-process this byte next time.
                    m_curr_packet.SetType(ETM3_PKT_NOTSYNC);    // send unsynced data packet.
                }
            }
            else
            {
                //save a byte - not start of a-sync
                m_currPacketData.push_back(currByte);

                // done all data in this block, or got 16 unsynced bytes
                if((bytesProcessed == dataBlockSize) || (m_currPacketData.size() == 16))
                {
                    bSendBlock = true;  // send none sync packet block
                    m_curr_packet.SetType(ETM3_PKT_NOTSYNC);    // send unsynced data packet.
                }
            }
        }
    }
    if(bSendBlock)
        SendPacket();
    return bytesProcessed;
}

ocsd_err_t EtmV3PktProcImpl::processHeaderByte(uint8_t by)
{
    InitPacketState();  // new packet, clear old single packet state (retains intra packet state).

    // save byte
    m_currPacketData.push_back(by);
   
    m_process_state = PROC_DATA;    // assume next is data packet

	// check for branch address 0bCxxxxxxx1
	if((by & 0x01) == 0x01 ) {
		m_curr_packet.SetType(ETM3_PKT_BRANCH_ADDRESS);
		m_BranchPktNeedsException = false;
		if((by & 0x80) != 0x80) {
			// no continuation - 1 byte branch same in alt and std...
            if((by == 0x01) && (m_interface->getComponentOpMode() & ETMV3_OPFLG_UNFORMATTED_SOURCE))
			{
                // TBD: need to fix up for handling bypassed ETM stream at some point.
                throwUnsupportedErr("Bypassed ETM stream not supported in this version of the decoder.");
                // could be EOTrace marker from bypassed formatter
				m_curr_packet.SetType(ETM3_PKT_BRANCH_OR_BYPASS_EOT);
			}
			else
            {
                OnBranchAddress();
				SendPacket();  // mark ready to send.
            }
		}
	}
	// check for p-header - 0b1xxxxxx0
	else if((by & 0x81) == 0x80) {
		m_curr_packet.SetType(ETM3_PKT_P_HDR);
        if(m_curr_packet.UpdateAtomFromPHdr(by,m_config.isCycleAcc()))
		    SendPacket();
        else
            throwPacketHeaderErr("Invalid P-Header.");
	}
	// check 0b0000xx00 group
	else if((by & 0xF3) == 0x00) {
			
		// 	A-Sync
		if(by == 0x00) {
			m_curr_packet.SetType(ETM3_PKT_A_SYNC);
		}
		// cycle count
		else if(by == 0x04) {
			m_curr_packet.SetType(ETM3_PKT_CYCLE_COUNT);
		}
		// I-Sync
		else if(by == 0x08) {
			m_curr_packet.SetType(ETM3_PKT_I_SYNC);
			m_bIsync_got_cycle_cnt = false;
			m_bIsync_get_LSiP_addr = false;							
		}
		// trigger
		else if(by == 0x0C) {
			m_curr_packet.SetType(ETM3_PKT_TRIGGER);
            // no payload - just send it.
			SendPacket();
		}
	}
	// check remaining 0bxxxxxx00 codes
	else if((by & 0x03 )== 0x00) {
		// OoO data 0b0xx0xx00
		if((by & 0x93 )== 0x00) {
            if(!m_config.isDataValTrace()) {
                m_curr_packet.SetErrType(ETM3_PKT_BAD_TRACEMODE);
                throwPacketHeaderErr("Invalid data trace header (out of order data) - not tracing data values.");
			}
			m_curr_packet.SetType(ETM3_PKT_OOO_DATA);
			uint8_t size = ((by & 0x0C) >> 2);
			// header contains a count of the data to follow
			// size 3 == 4 bytes, other sizes == size bytes
			if(size == 0)
            {
                m_curr_packet.SetDataOOOTag((by >> 5)  & 0x3);
                m_curr_packet.SetDataValue(0);
				SendPacket();
            }
			else
				m_bytesExpectedThisPkt = (short)(1 + ((size == 3) ? 4 : size));
		}
		// I-Sync + cycle count
		else if(by == 0x70) {
			m_curr_packet.SetType(ETM3_PKT_I_SYNC_CYCLE);
			m_bIsync_got_cycle_cnt = false;
			m_bIsync_get_LSiP_addr = false;			
		}
		// store failed
		else if(by == 0x50) {
            if(!m_config.isDataValTrace())
            {
                m_curr_packet.SetErrType(ETM3_PKT_BAD_TRACEMODE);
                throwPacketHeaderErr("Invalid data trace header (store failed) - not tracing data values.");
            }
            m_curr_packet.SetType(ETM3_PKT_STORE_FAIL);
            SendPacket();
		}
		// OoO placeholder 0b01x1xx00
		else if((by & 0xD3 )== 0x50) {
			m_curr_packet.SetType(ETM3_PKT_OOO_ADDR_PLC);
            if(!m_config.isDataTrace())
            {
                m_curr_packet.SetErrType(ETM3_PKT_BAD_TRACEMODE);
                throwPacketHeaderErr("Invalid data trace header (out of order placeholder) - not tracing data.");
            }
            // expecting data address if flagged and address tracing enabled (flag can be set even if address tracing disabled)
			m_bExpectingDataAddress = ((by & DATA_ADDR_EXPECTED_FLAG) == DATA_ADDR_EXPECTED_FLAG) && m_config.isDataAddrTrace();
			m_bFoundDataAddress = false;
            m_curr_packet.SetDataOOOTag((by >> 2) & 0x3);
			if(!m_bExpectingDataAddress) {
				SendPacket();
			}
		}
        // vmid 0b00111100 
        else if(by == 0x3c) {
            m_curr_packet.SetType(ETM3_PKT_VMID);
        }
		else
		{
			m_curr_packet.SetErrType(ETM3_PKT_RESERVED);
            throwPacketHeaderErr("Packet header reserved encoding");
		}
	}
	// normal data 0b00x0xx10
	else if((by & 0xD3 )== 0x02) {
		uint8_t size = ((by & 0x0C) >> 2);
		if(!m_config.isDataTrace()) {
            m_curr_packet.SetErrType(ETM3_PKT_BAD_TRACEMODE);
            throwPacketHeaderErr("Invalid data trace header (normal data) - not tracing data.");
		}
		m_curr_packet.SetType(ETM3_PKT_NORM_DATA);
		m_bExpectingDataAddress = ((by & DATA_ADDR_EXPECTED_FLAG) == DATA_ADDR_EXPECTED_FLAG) && m_config.isDataAddrTrace();
		m_bFoundDataAddress = false;

        // set this with the data bytes expected this packet, plus the header byte.
		m_bytesExpectedThisPkt = (short)( 1 + ((size == 3) ? 4 : size));
		if(!m_bExpectingDataAddress && (m_bytesExpectedThisPkt == 1)) {
			// single byte data packet, value = 0;
            m_curr_packet.SetDataValue(0);
			SendPacket();
		}

	}
	// data suppressed 0b01100010
	else if(by == 0x62) {
		if(!m_config.isDataTrace())
        {
            m_curr_packet.SetErrType(ETM3_PKT_BAD_TRACEMODE);
            throwPacketHeaderErr("Invalid data trace header (data suppressed) - not tracing data.");
        }
        m_curr_packet.SetType(ETM3_PKT_DATA_SUPPRESSED);
        SendPacket();
	}
	// value not traced 0b011x1010
	else if((by & 0xEF )== 0x6A) {
		if(!m_config.isDataTrace()) {
            m_curr_packet.SetErrType(ETM3_PKT_BAD_TRACEMODE);
            throwPacketHeaderErr("Invalid data trace header (value not traced) - not tracing data.");
		}
		m_curr_packet.SetType(ETM3_PKT_VAL_NOT_TRACED);
		m_bExpectingDataAddress = ((by & DATA_ADDR_EXPECTED_FLAG) == DATA_ADDR_EXPECTED_FLAG) && m_config.isDataAddrTrace();
		m_bFoundDataAddress = false;
		if(!m_bExpectingDataAddress) {
			SendPacket();
        }
	}
	// ignore 0b01100110
	else if(by == 0x66) {
		m_curr_packet.SetType(ETM3_PKT_IGNORE);
        SendPacket();					
	}
	// context ID 0b01101110
	else if(by == 0x6E) {
		m_curr_packet.SetType(ETM3_PKT_CONTEXT_ID);
        m_bytesExpectedThisPkt = (short)(1 + m_config.CtxtIDBytes());
	}
	// exception return 0b01110110
	else if(by == 0x76) {
		m_curr_packet.SetType(ETM3_PKT_EXCEPTION_EXIT);
        SendPacket();
	}
	// exception entry 0b01111110
	else if(by == 0x7E) {
		m_curr_packet.SetType(ETM3_PKT_EXCEPTION_ENTRY);
        SendPacket();
	}
	// timestamp packet 0b01000x10
	else if((by & 0xFB )== 0x42)
	{
		m_curr_packet.SetType(ETM3_PKT_TIMESTAMP);
	}
	else
	{
		m_curr_packet.SetErrType(ETM3_PKT_RESERVED);
        throwPacketHeaderErr("Packet header reserved encoding.");
	}
    return OCSD_OK;
}

ocsd_err_t EtmV3PktProcImpl::processPayloadByte(uint8_t by)
{
    bool bTopBitSet = false;
    bool packetDone = false;

	// pop byte into buffer
    m_currPacketData.push_back(by);
				
    switch(m_curr_packet.getType()) {
	default:
        throw ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_PKT_INTERP_FAIL,m_packet_index,m_chanIDCopy,"Interpreter failed - cannot process payload for unexpected or unsupported packet.");
		break;
     	
	case ETM3_PKT_BRANCH_ADDRESS:
		bTopBitSet = (bool)((by & 0x80) == 0x80);
        if(m_config.isAltBranch())  // etm implements the alternative branch encoding
        {
			if(!bTopBitSet)     // no continuation
            {
				if(!m_BranchPktNeedsException)
				{
					if((by & 0xC0) == 0x40) 
						m_BranchPktNeedsException = true;
					else
                        packetDone = true;
				}
				else
                    packetDone = true;
			}				
		}
		else 
        {
			// standard encoding  < 5 bytes cannot be exception branch
			// 5 byte packet
            if(m_currPacketData.size() == 5) {
				if((by & 0xC0) == 0x40) 
					// expecting follow up byte(s)
					m_BranchPktNeedsException = true;
				else
                    packetDone = true;						
			}
			// waiting for exception packet
			else if(m_BranchPktNeedsException){
				if(!bTopBitSet)
					packetDone = true;
			}
			else {
				// not exception - end of packets
				if(!bTopBitSet)
					packetDone = true;				
			}
		}

        if(packetDone)
        {
            OnBranchAddress();
			SendPacket();
        }
		break;

	case ETM3_PKT_BRANCH_OR_BYPASS_EOT:
        /*
		if((by != 0x00) || ( m_currPacketData.size() == ETM3_PKT_BUFF_SIZE)) {
			if(by == 0x80 && ( m_currPacketData.size() == 7)) {
				// branch 0 followed by A-sync!
				m_currPacketData.size() = 1;
                m_curr_packet.SetType(ETM3_PKT_BRANCH_ADDRESS;
				SendPacket();
                memcpy(m_currPacketData, &m_currPacketData[1],6);
				m_currPacketData.size() = 6;
                m_curr_packet.SetType(ETM3_PKT_A_SYNC;
				SendPacket();
			}
			else if( m_currPacketData.size() == 2) {
				// branch followed by another byte
                m_currPacketData.size() = 1;
                m_curr_packet.SetType(ETM3_PKT_BRANCH_ADDRESS;
                SendPacket();
                ProcessHeaderByte(by);
			}
			else if(by == 0x00) {
				// end of buffer...output something - incomplete / unknown.
				SendPacket();
			}
			else if(by == 0x01) {
				// 0x01 - 0x00 x N - 0x1
				// end of buffer...output something
                m_currPacketData.size()--;
                SendPacket();
				ProcessHeaderByte(by);					
			}
			else {
				// branch followed by unknown sequence
				int oldidx =  m_currPacketData.size();
                m_currPacketData.size() = 1;
                m_curr_packet.SetType(ETM3_PKT_BRANCH_ADDRESS;
				SendPacket();
                oldidx--;
                memcpy(m_currPacketData, &m_currPacketData[1],oldidx);
                m_currPacketData.size() = oldidx;
                SendBadPacket("ERROR : unknown sequence");
			}
		}*/
		// just ignore zeros
		break;
			
		

	case ETM3_PKT_A_SYNC:
		if(by == 0x00) {
			if( m_currPacketData.size() > 5) {
				// extra 0, need to lose one
                
                // set error type
                m_curr_packet.SetErrType(ETM3_PKT_BAD_SEQUENCE);
                // mark extra 0 for sending, retain remaining, restart in A-SYNC processing mode.
                setBytesPartPkt(1,PROC_DATA,ETM3_PKT_A_SYNC);   
                throwMalformedPacketErr("A-Sync ?: Extra 0x00 in sequence");
			}
		}
		else if((by == 0x80) && ( m_currPacketData.size() == 6)) {
			SendPacket();
			m_bStreamSync = true;
		}
		else
		{
            m_curr_packet.SetErrType(ETM3_PKT_BAD_SEQUENCE);
            m_bytesProcessed--; // remove the last byte from the number processed to re-try
            m_currPacketData.pop_back();  // remove the last byte processed from the packet
            throwMalformedPacketErr("A-Sync ? : Unexpected byte in sequence");
		}
		break;			
			
	case ETM3_PKT_CYCLE_COUNT:
		bTopBitSet = ((by & 0x80) == 0x80);
        if(!bTopBitSet || ( m_currPacketData.size() >= 6)) {
            m_currPktIdx = 1;
            m_curr_packet.SetCycleCount(extractCycleCount());
			SendPacket();
		}
		break;
			
	case ETM3_PKT_I_SYNC_CYCLE:
		if(!m_bIsync_got_cycle_cnt) {
			if(((by & 0x80) != 0x80) || ( m_currPacketData.size() >= 6)) {
				m_bIsync_got_cycle_cnt = true;
			}
			break;
		}
		// fall through when we have the first non-cycle count byte
	case ETM3_PKT_I_SYNC:
		if(m_bytesExpectedThisPkt == 0) {
			int cycCountBytes = m_currPacketData.size() - 2;
            int ctxtIDBytes = m_config.CtxtIDBytes();
			// bytes expected = header + n x ctxt id + info byte + 4 x addr; 
            if(m_config.isInstrTrace())
				m_bytesExpectedThisPkt = cycCountBytes + 6 + ctxtIDBytes;
			else
				m_bytesExpectedThisPkt = 2 + ctxtIDBytes;
			m_IsyncInfoIdx = 1 + cycCountBytes + ctxtIDBytes;
		}
		if(( m_currPacketData.size() - 1) == (unsigned)m_IsyncInfoIdx) {
			m_bIsync_get_LSiP_addr = ((m_currPacketData[m_IsyncInfoIdx] & 0x80) == 0x80);
		}
			
		// if bytes collected >= bytes expected
		if( m_currPacketData.size() >= m_bytesExpectedThisPkt) {
			// if we still need the LSip Addr, then this is not part of the expected
			// count as we have no idea how long it is
			if(m_bIsync_get_LSiP_addr) {
				if((by & 0x80) != 0x80) {
                    OnISyncPacket();
				}
			}
			else {
				// otherwise, output now
                OnISyncPacket();
			}
		}
		break;
			
	case ETM3_PKT_NORM_DATA:
		if(m_bExpectingDataAddress && !m_bFoundDataAddress) {
			// look for end of continuation bits
			if((by & 0x80) != 0x80) {
				m_bFoundDataAddress = true;
				// add on the bytes we have found for the address to the expected data bytes
				m_bytesExpectedThisPkt += ( m_currPacketData.size() - 1);   					
			}
			else 
				break;
		}
		// found any data address we were expecting
		else if(m_bytesExpectedThisPkt == m_currPacketData.size()) {
            m_currPktIdx = 1;
            if(m_bExpectingDataAddress)
            {
                uint8_t bits = 0, beVal = 0;
                bool updateBE = false;
                uint32_t dataAddress = extractDataAddress(bits,updateBE,beVal);
                m_curr_packet.UpdateDataAddress(dataAddress, bits);
                if(updateBE)
                    m_curr_packet.UpdateDataEndian(beVal);
            }
            m_curr_packet.SetDataValue(extractDataValue((m_currPacketData[0] >> 2) & 0x3));
			SendPacket();
		}
		break;
			
	case ETM3_PKT_OOO_DATA:
		if(m_bytesExpectedThisPkt ==  m_currPacketData.size())
        {
            m_currPktIdx = 1;
            m_curr_packet.SetDataValue(extractDataValue((m_currPacketData[0] >> 2) & 0x3));
            m_curr_packet.SetDataOOOTag((m_currPacketData[0] >> 5) & 0x3);
			SendPacket();
        }
		if(m_bytesExpectedThisPkt <  m_currPacketData.size())
			throwMalformedPacketErr("Malformed out of order data packet.");
		break;
			
        // both these expect an address only.
	case ETM3_PKT_VAL_NOT_TRACED:
    case ETM3_PKT_OOO_ADDR_PLC: // we set the tag earlier.
		if(m_bExpectingDataAddress) {
			// look for end of continuation bits
			if((by & 0x80) != 0x80) {
                uint8_t bits = 0, beVal = 0;
                bool updateBE = false;
                m_currPktIdx = 1;
                uint32_t dataAddress = extractDataAddress(bits,updateBE,beVal);
                m_curr_packet.UpdateDataAddress(dataAddress, bits);
                if(updateBE)
                    m_curr_packet.UpdateDataEndian(beVal);
				SendPacket();
			}
		}
		break;
					
	case ETM3_PKT_CONTEXT_ID:
		if(m_bytesExpectedThisPkt == m_currPacketData.size()) {
            m_currPktIdx = 1;
            m_curr_packet.UpdateContextID(extractCtxtID());
			SendPacket();
		}
		if(m_bytesExpectedThisPkt < m_currPacketData.size())
			throwMalformedPacketErr("Malformed context id packet.");
		break;
			
	case ETM3_PKT_TIMESTAMP:
		if((by & 0x80) != 0x80) {
            uint8_t tsBits = 0;
            m_currPktIdx = 1;
            uint64_t tsVal = extractTimestamp(tsBits);
            m_curr_packet.UpdateTimestamp(tsVal,tsBits);
			SendPacket();
		}
        break;

    case ETM3_PKT_VMID:
        // single byte payload
        m_curr_packet.UpdateVMID(by);
        SendPacket();
        break;
	}

    return OCSD_OK;
}

// extract branch address packet at current location in packet data.
void EtmV3PktProcImpl::OnBranchAddress()
{
    int validBits = 0;
    ocsd_vaddr_t partAddr = 0;
  
    partAddr = extractBrAddrPkt(validBits);
    m_curr_packet.UpdateAddress(partAddr,validBits);
}

uint32_t EtmV3PktProcImpl::extractBrAddrPkt(int &nBitsOut)
{
    static int addrshift[] = {
        2, // ARM_ISA
        1, // thumb
        1, // thumb EE
        0  // jazelle
    };

    static uint8_t addrMask[] = {  // byte 5 masks
        0x7, // ARM_ISA
        0xF, // thumb
        0xF, // thumb EE
        0x1F  // jazelle
    };

    static int addrBits[] = { // address bits in byte 5
        3, // ARM_ISA
        4, // thumb
        4, // thumb EE
        5  // jazelle
    };

    static ocsd_armv7_exception exceptionTypeARMdeprecated[] = {
        Excp_Reset,
        Excp_IRQ,
        Excp_Reserved,
        Excp_Reserved,
        Excp_Jazelle,
        Excp_FIQ,
        Excp_AsyncDAbort,
        Excp_DebugHalt
    };

    bool CBit = true;
    int bytecount = 0;
    int bitcount = 0;
    int shift = 0;
    int isa_idx = 0;
    uint32_t value = 0;
    uint8_t addrbyte;
    bool byte5AddrUpdate = false;

    while(CBit && bytecount < 4)
    {
        checkPktLimits();
        addrbyte = m_currPacketData[m_currPktIdx++];
        CBit = (bool)((addrbyte & 0x80) != 0);
        shift = bitcount;
        if(bytecount == 0)
        {
            addrbyte &= ~0x81;
            bitcount+=6;
            addrbyte >>= 1;
        }
        else
        {
            // bytes 2-4, no continuation, alt format uses bit 6 to indicate following exception bytes
            if(m_config.isAltBranch() && !CBit)
            {
                // last compressed address byte with exception
                if((addrbyte & 0x40) == 0x40)
                    extractExceptionData();
                addrbyte &= 0x3F;
                bitcount+=6;       
            }
            else
            {
                addrbyte &= 0x7F;
                bitcount+=7;
            }
        }
        value |= ((uint32_t)addrbyte) << shift;
        bytecount++;
    }

    // byte 5 - indicates following exception bytes (or not!)
    if(CBit)
    {
        checkPktLimits();
        addrbyte = m_currPacketData[m_currPktIdx++];
        
        // deprecated original byte 5 encoding - ARM state exception only
        if(addrbyte & 0x80)
        {
            uint8_t excep_num = (addrbyte >> 3) & 0x7;
            m_curr_packet.UpdateISA(ocsd_isa_arm);
            m_curr_packet.SetException(exceptionTypeARMdeprecated[excep_num], excep_num, (addrbyte & 0x40) ? true : false,m_config.isV7MArch());
        }
        else
        // normal 5 byte branch, or uses exception bytes.
        {
            // go grab the exception bits to correctly interpret the ISA state
            if((addrbyte & 0x40) == 0x40)
                extractExceptionData();     

            if((addrbyte & 0xB8) == 0x08)
                m_curr_packet.UpdateISA(ocsd_isa_arm);
            else if ((addrbyte & 0xB0) == 0x10)
                m_curr_packet.UpdateISA(m_curr_packet.AltISA() ? ocsd_isa_tee : ocsd_isa_thumb2);
            else if ((addrbyte & 0xA0) == 0x20)
                m_curr_packet.UpdateISA(ocsd_isa_jazelle);
            else
                throwMalformedPacketErr("Malformed Packet - Unknown ISA.");
        }

        byte5AddrUpdate = true; // need to update the address value from byte 5
    }

    // figure out the correct ISA shifts for the address bits
    switch(m_curr_packet.ISA()) 
    {
    case ocsd_isa_thumb2: isa_idx = 1; break;
    case ocsd_isa_tee: isa_idx = 2; break;
    case ocsd_isa_jazelle: isa_idx = 3; break;
    default: break;
    }

    if(byte5AddrUpdate)
    {
        value |= ((uint32_t)(addrbyte & addrMask[isa_idx])) << bitcount;
        bitcount += addrBits[isa_idx];
    }
    
    // finally align according to ISA
    shift = addrshift[isa_idx];
    value <<= shift;
    bitcount += shift;

    nBitsOut = bitcount;
    return value;
}

// extract exception data from bytes after address.
void EtmV3PktProcImpl::extractExceptionData()
{
    static const ocsd_armv7_exception exceptionTypesStd[] = {
        Excp_NoException, Excp_DebugHalt, Excp_SMC, Excp_Hyp,
        Excp_AsyncDAbort, Excp_Jazelle, Excp_Reserved, Excp_Reserved,
        Excp_Reset, Excp_Undef, Excp_SVC, Excp_PrefAbort, 
        Excp_SyncDataAbort, Excp_Generic, Excp_IRQ, Excp_FIQ
    };

    static const ocsd_armv7_exception exceptionTypesCM[] = {
        Excp_NoException, Excp_CMIRQn, Excp_CMIRQn, Excp_CMIRQn,
        Excp_CMIRQn, Excp_CMIRQn, Excp_CMIRQn, Excp_CMIRQn, 
        Excp_CMIRQn, Excp_CMUsageFault, Excp_CMNMI, Excp_SVC, 
        Excp_CMDebugMonitor, Excp_CMMemManage, Excp_CMPendSV, Excp_CMSysTick,
        Excp_Reserved, Excp_Reset,  Excp_Reserved, Excp_CMHardFault,
        Excp_Reserved, Excp_CMBusFault, Excp_Reserved, Excp_Reserved
    };

    uint16_t exceptionNum = 0;
    ocsd_armv7_exception excep_type = Excp_Reserved;
    int resume = 0;
    int irq_n = 0;
    bool cancel_prev_instr = 0;
    bool Byte2 = false;

    checkPktLimits();

    //**** exception info Byte 0
    uint8_t dataByte =  m_currPacketData[m_currPktIdx++];   

    m_curr_packet.UpdateNS(dataByte & 0x1);
    exceptionNum |= (dataByte >> 1) & 0xF;
    cancel_prev_instr = (dataByte & 0x20) ? true : false;
    m_curr_packet.UpdateAltISA(((dataByte & 0x40) != 0)  ? 1 : 0); 

    //** another byte?
    if(dataByte & 0x80)
    {
        checkPktLimits();
        dataByte = m_currPacketData[m_currPktIdx++];

        if(dataByte & 0x40)
            Byte2 = true;   //** immediate info byte 2, skipping 1
        else
        {
            //**** exception info Byte 1
            if(m_config.isV7MArch())
            {
                exceptionNum |= ((uint16_t)(dataByte & 0x1F)) << 4;
            }
             m_curr_packet.UpdateHyp(dataByte & 0x20 ? 1 : 0);

            if(dataByte & 0x80)
            {
                checkPktLimits();
                dataByte = m_currPacketData[m_currPktIdx++];
                Byte2 = true;
            }
        }
        //**** exception info Byte 2
        if(Byte2)
        {
            resume = dataByte & 0xF;
        }
    }

    // set the exception type - according to the number and core profile
    if(m_config.isV7MArch())
    {
       exceptionNum &= 0x1FF;
        if(exceptionNum < 0x018)
            excep_type= exceptionTypesCM[exceptionNum];
        else
            excep_type = Excp_CMIRQn;

        if(excep_type == Excp_CMIRQn)
        {
            if(exceptionNum > 0x018)
                irq_n = exceptionNum - 0x10;
            else if(exceptionNum == 0x008)
                irq_n = 0;
            else
                irq_n = exceptionNum;
        }
    }
    else
    {
        exceptionNum &= 0xF;
        excep_type = exceptionTypesStd[exceptionNum];
    }
    m_curr_packet.SetException(excep_type, exceptionNum, cancel_prev_instr,m_config.isV7MArch(), irq_n,resume);
}

void EtmV3PktProcImpl::checkPktLimits()
{
    // index running off the end of the packet means a malformed packet.
    if(m_currPktIdx >= m_currPacketData.size())
        throwMalformedPacketErr("Malformed Packet - oversized packet.");
}

uint32_t EtmV3PktProcImpl::extractCtxtID()
{
    uint32_t ctxtID = 0;
    int size = m_config.CtxtIDBytes();

    // check we have enough data
    if((m_currPktIdx + size) > m_currPacketData.size())
        throwMalformedPacketErr("Too few bytes to extract context ID.");

    switch(size)
    {
    case 1:
        ctxtID = (uint32_t)m_currPacketData[m_currPktIdx];
        m_currPktIdx++;
        break;

    case 2:
        ctxtID = (uint32_t)m_currPacketData[m_currPktIdx] 
                    | ((uint32_t)m_currPacketData[m_currPktIdx+1]) << 8;
        m_currPktIdx+=2;
        break;

    case 4:
        ctxtID = (uint32_t)m_currPacketData[m_currPktIdx] 
                    | ((uint32_t)m_currPacketData[m_currPktIdx+1]) << 8
                    | ((uint32_t)m_currPacketData[m_currPktIdx+2]) << 16
                    | ((uint32_t)m_currPacketData[m_currPktIdx+3]) << 24;
        m_currPktIdx+=4;
        break;
    }
    return ctxtID;
}

uint64_t EtmV3PktProcImpl::extractTimestamp(uint8_t &tsBits)
{
    uint64_t ts = 0;
    unsigned tsMaxBytes = m_config.TSPkt64() ? 9 : 7;
    unsigned tsCurrBytes = 0;
    bool bCont = true;
    uint8_t mask = 0x7F;
    uint8_t last_mask = m_config.TSPkt64() ? 0xFF : 0x3F;
    uint8_t ts_iter_bits = 7;
    uint8_t ts_last_iter_bits = m_config.TSPkt64() ? 8 : 6;
    uint8_t currByte;
    tsBits = 0;

    while((tsCurrBytes < tsMaxBytes) && bCont)
    {
        if(m_currPacketData.size() < (m_currPktIdx + tsCurrBytes + 1))
            throwMalformedPacketErr("Insufficient bytes to extract timestamp.");

        currByte = m_currPacketData[m_currPktIdx+tsCurrBytes];
        ts |= ((uint64_t)(currByte & mask)) << (7 * tsCurrBytes);
        tsCurrBytes++;
        tsBits += ts_iter_bits;
        bCont = ((0x80 & currByte) == 0x80);
        if(tsCurrBytes == (tsMaxBytes - 1))
        {
            mask = last_mask;
            ts_iter_bits = ts_last_iter_bits;
        }
    }
    m_currPktIdx += tsCurrBytes;
    return ts;
}


uint32_t EtmV3PktProcImpl::extractDataAddress(uint8_t &bits, bool &updateBE, uint8_t &beVal)
{
    uint32_t dataAddr = 0;
    int bytesIdx = 0;
    bool bCont = true;
    uint8_t currByte = 0;

    updateBE = false;
    bits = 0;

    while(bCont)
    {
        checkPktLimits();
        currByte = m_currPacketData[m_currPktIdx++] & ((bytesIdx == 4) ? 0x0F : 0x7F);
        dataAddr |= (((uint32_t)currByte)  << (bytesIdx * 7));
        bCont = ((currByte & 0x80) == 0x80);
        if(bytesIdx == 4)
        {
            bits += 4;
            updateBE = true;
            beVal = ((currByte >> 4) & 0x1);
            bCont = false;
        }
        else
            bits+=7;
        bytesIdx++;
    }    
    return dataAddr;
}

uint32_t EtmV3PktProcImpl::extractDataValue(const int dataByteSize)
{
    static int bytesReqTable[] = { 0,1,2,4 };

    uint32_t dataVal = 0;
    int bytesUsed = 0;
    int bytesReq = bytesReqTable[dataByteSize & 0x3]; 
    while(bytesUsed < bytesReq)
    {
        checkPktLimits();
        dataVal |= (((uint32_t)m_currPacketData[m_currPktIdx++])  << (bytesUsed * 8));
        bytesUsed++;
    }
    return dataVal;
}


uint32_t EtmV3PktProcImpl::extractCycleCount()
{
    uint32_t cycleCount = 0;
    int byteIdx = 0;
    uint8_t mask = 0x7F;
    bool bCond = true;
    uint8_t currByte = 0;

    while(bCond)
    {
        checkPktLimits();
        currByte = m_currPacketData[m_currPktIdx++];
        cycleCount |= ((uint32_t)(currByte & mask)) << (7 * byteIdx);
        bCond = ((currByte & 0x80) == 0x80);
        byteIdx++;

        if(byteIdx == 4)
            mask = 0x0F;

        if(byteIdx == 5)
            bCond = false;
    }
    return cycleCount;
}

void EtmV3PktProcImpl::OnISyncPacket()
{
    uint8_t iSyncInfoByte = 0;
    uint32_t instrAddr = 0, LSiPAddr = 0;
    int LSiPBits = 0;
    uint8_t T = 0, J = 0, AltISA = 0;

    m_currPktIdx = 1;
    if(m_bIsync_got_cycle_cnt)
    {
        m_curr_packet.SetCycleCount(extractCycleCount());
        m_curr_packet.SetISyncHasCC();
    }

    if(m_config.CtxtIDBytes() != 0)
    {
        m_curr_packet.UpdateContextID(extractCtxtID());
    }

    // extract context info 
    iSyncInfoByte = m_currPacketData[m_currPktIdx++];
    m_curr_packet.SetISyncReason((ocsd_iSync_reason)((iSyncInfoByte >> 5) & 0x3));
    J = (iSyncInfoByte >> 4) & 0x1;
    AltISA = m_config.MinorRev() >= 3 ? (iSyncInfoByte >> 2) & 0x1 : 0;
    m_curr_packet.UpdateNS((iSyncInfoByte >> 3) & 0x1);
    if(m_config.hasVirtExt())
        m_curr_packet.UpdateHyp((iSyncInfoByte >> 1) & 0x1);
    
    // main address value - full 32 bit address value
    if(m_config.isInstrTrace())
    {
        for(int i = 0; i < 4; i++)
            instrAddr |= ((uint32_t)m_currPacketData[m_currPktIdx++]) << (8*i);
        T = instrAddr & 0x1;    // get the T bit.
        instrAddr &= ~0x1;      // remove from address.
        m_curr_packet.UpdateAddress(instrAddr,32);  

        // enough data now to set the instruction set.
        ocsd_isa currISA = ocsd_isa_arm;
        if(J)
            currISA = ocsd_isa_jazelle;
        else if(T)
            currISA = AltISA ? ocsd_isa_tee : ocsd_isa_thumb2;
        m_curr_packet.UpdateISA(currISA);

        // possible follow up address value - rarely uses unless trace enabled during
        // load and store instruction executing on top of other instruction. 
        if(m_bIsync_get_LSiP_addr)
        {
            LSiPAddr = extractBrAddrPkt(LSiPBits);
            // follow up address value is compressed relative to the main value
            // we store this in the data address value temporarily.
            m_curr_packet.UpdateDataAddress(instrAddr,32);
            m_curr_packet.UpdateDataAddress(LSiPAddr,LSiPBits);
        }
    }
    else
        m_curr_packet.SetISyncNoAddr();
         
    SendPacket(); // mark ready to send
}

/* End of File trc_pkt_proc_etmv3_impl.cpp */
