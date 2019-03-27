/*
 * \file       trc_pkt_decode_stm.cpp
 * \brief      OpenCSD : STM packet decoder - output generic SW trace packets.
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

#include "opencsd/stm/trc_pkt_decode_stm.h"
#define DCD_NAME "DCD_STM"

TrcPktDecodeStm::TrcPktDecodeStm()
    : TrcPktDecodeBase(DCD_NAME)
{
    initDecoder();
}

TrcPktDecodeStm::TrcPktDecodeStm(int instIDNum)
    : TrcPktDecodeBase(DCD_NAME, instIDNum)
{
    initDecoder();
}

TrcPktDecodeStm::~TrcPktDecodeStm()
{
    if(m_payload_buffer)
        delete [] m_payload_buffer;
    m_payload_buffer = 0;
}

/* implementation packet decoding interface */
ocsd_datapath_resp_t TrcPktDecodeStm::processPacket()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    bool bPktDone = false;

    m_decode_pass1 = true;

    while(!bPktDone)
    {
        switch(m_curr_state)
        {
        case NO_SYNC:
            m_output_elem.setType(OCSD_GEN_TRC_ELEM_NO_SYNC);
            resp = outputTraceElement(m_output_elem);
            m_curr_state = WAIT_SYNC;
            break;

        case WAIT_SYNC:
            if(m_curr_packet_in->getPktType() == STM_PKT_ASYNC)
                m_curr_state = DECODE_PKTS;
            bPktDone = true;
            break;

        case DECODE_PKTS:
            resp = decodePacket(bPktDone);
            break;
        }
    }
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodeStm::onEOT()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    m_output_elem.setType(OCSD_GEN_TRC_ELEM_EO_TRACE);
    resp = outputTraceElement(m_output_elem);
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodeStm::onReset()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    resetDecoder();
    return resp;
}

ocsd_datapath_resp_t TrcPktDecodeStm::onFlush()
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    // don't currently save unsent packets so nothing to flush
    return resp;
}

ocsd_err_t TrcPktDecodeStm::onProtocolConfig()
{
    if(m_config == 0)
        return OCSD_ERR_NOT_INIT;

    // static config - copy of CSID for easy reference
    m_CSID = m_config->getTraceID();
    return OCSD_OK;
}

void TrcPktDecodeStm::initDecoder()
{
    m_payload_buffer = 0;
    m_num_pkt_correlation = 1;  // fixed at single packet payload correlation - add feature later
    m_CSID = 0;

    // base decoder state - STM requires no memory and instruction decode.
    setUsesMemAccess(false);
    setUsesIDecode(false);

    resetDecoder();
}

void TrcPktDecodeStm::resetDecoder()
{
    m_curr_state = NO_SYNC;
    m_payload_size = 0;     
    m_payload_used = 0; 
    m_payload_odd_nibble = false;
    m_output_elem.init();
    m_swt_packet_info.swt_flag_bits = 0;	// zero out everything
    initPayloadBuffer();
}

void TrcPktDecodeStm::initPayloadBuffer()
{
    // set up the payload buffer. If we are correlating indentical packets then 
    // need a buffer that is a multiple of 64bit packets.
    // otherwise a single packet length will do.
    if(m_payload_buffer)
        delete [] m_payload_buffer;
    m_payload_buffer = new (std::nothrow) uint8_t[m_num_pkt_correlation * sizeof(uint64_t)];
}

ocsd_datapath_resp_t TrcPktDecodeStm::decodePacket(bool &bPktDone)
{
    ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
    bool bSendPacket = false;       // flag to indicate output required.

    bPktDone = true;    // assume complete unless 2nd pass required.
    m_output_elem.setType(OCSD_GEN_TRC_ELEM_SWTRACE);
    clearSWTPerPcktInfo();
    
    switch (m_curr_packet_in->getPktType())
    {
    case STM_PKT_BAD_SEQUENCE:   /**< Incorrect protocol sequence */
    case STM_PKT_RESERVED:
        resp = OCSD_RESP_FATAL_INVALID_DATA;
    case STM_PKT_NOTSYNC:
        resetDecoder();
        break;

    case STM_PKT_VERSION:    /**< Version packet  - not relevant to generic (versionless) o/p */
    case STM_PKT_ASYNC:      /**< Alignment synchronisation packet */
    case STM_PKT_INCOMPLETE_EOT:     /**< Incomplete packet flushed at end of trace. */
        // no action required.
        break;

/* markers for valid packets*/
    case STM_PKT_NULL:       /**< Null packet */
        if(m_curr_packet_in->isTSPkt())
            bSendPacket = true;     // forward NULL packet if associated timestamp.
        break;
        
    case STM_PKT_FREQ:       /**< Frequency packet */
        m_swt_packet_info.swt_frequency = 1;
        updatePayload(bSendPacket);
        break;

    case STM_PKT_TRIG:       /**< Trigger event packet. */
        m_swt_packet_info.swt_trigger_event = 1;
        updatePayload(bSendPacket);
        break;

    case STM_PKT_GERR:       /**< Global error packet - protocol error but unknown which master had error */
        m_swt_packet_info.swt_master_id = m_curr_packet_in->getMaster();
        m_swt_packet_info.swt_channel_id = m_curr_packet_in->getChannel(); 
        m_swt_packet_info.swt_global_err = 1;
        m_swt_packet_info.swt_id_valid = 0;
        updatePayload(bSendPacket);
        break;

    case STM_PKT_MERR:       /**< Master error packet - current master detected an error (e.g. dropped trace) */
        m_swt_packet_info.swt_channel_id = m_curr_packet_in->getChannel(); 
        m_swt_packet_info.swt_master_err = 1;
        updatePayload(bSendPacket);
        break;

    case STM_PKT_M8:         /**< Set current master */
        m_swt_packet_info.swt_master_id = m_curr_packet_in->getMaster();
        m_swt_packet_info.swt_channel_id = m_curr_packet_in->getChannel();  // forced to 0
        m_swt_packet_info.swt_id_valid = 1;
        break;

    case STM_PKT_C8:         /**< Set lower 8 bits of current channel - packet proc hadnles this */
    case STM_PKT_C16:        /**< Set current channel */
        m_swt_packet_info.swt_channel_id = m_curr_packet_in->getChannel(); 
        break;

    case STM_PKT_FLAG:       /**< Flag packet */
        m_swt_packet_info.swt_marker_packet = 1;
        bSendPacket = true;  // send 0 payload marker packet./
        break;
        

    case STM_PKT_D4:         /**< 4 bit data payload packet */
    case STM_PKT_D8:         /**< 8 bit data payload packet */
    case STM_PKT_D16:        /**< 16 bit data payload packet */
    case STM_PKT_D32:        /**< 32 bit data payload packet */
    case STM_PKT_D64:        /**< 64 bit data payload packet */
        updatePayload(bSendPacket);
        break;

    }

    if(bSendPacket)
    {
        if(m_curr_packet_in->isTSPkt())
        {
            m_output_elem.setTS(m_curr_packet_in->getTSVal());
            m_swt_packet_info.swt_has_timestamp = 1;
        }
        m_output_elem.setSWTInfo(m_swt_packet_info);
        resp = outputTraceElement(m_output_elem);
    }
    
    return resp;
}

void TrcPktDecodeStm::clearSWTPerPcktInfo()
{
    m_swt_packet_info.swt_flag_bits &= (uint32_t)(0x0 | SWT_ID_VALID_MASK);    // clear flags and current payload size (save id valid flag).
}

void TrcPktDecodeStm::updatePayload(bool &bSendPacket)
{
    // without buffering similar packets - this function is quite simple
    bSendPacket = true;
    m_swt_packet_info.swt_payload_num_packets = 1;

    switch(m_curr_packet_in->getPktType())
    {
    case STM_PKT_D4:         /**< 4 bit data payload packet */
        m_swt_packet_info.swt_payload_pkt_bitsize = 4;
        *(uint8_t *)m_payload_buffer = m_curr_packet_in->getD4Val();
        break;

    case STM_PKT_D8:         /**< 8 bit data payload packet */
    case STM_PKT_TRIG:       /**< Trigger event packet - 8 bits. */
    case STM_PKT_GERR:       /**< error packet - 8 bits. */
    case STM_PKT_MERR:       /**< error packet - 8 bits. */
        m_swt_packet_info.swt_payload_pkt_bitsize = 8;
        *(uint8_t *)m_payload_buffer = m_curr_packet_in->getD8Val();
        break;

    case STM_PKT_D16:        /**< 16 bit data payload packet */
        m_swt_packet_info.swt_payload_pkt_bitsize = 16;
        *(uint16_t *)m_payload_buffer = m_curr_packet_in->getD16Val();
        break;

    case STM_PKT_D32:        /**< 32 bit data payload packet */
    case STM_PKT_FREQ:       /**< Frequency packet */
        m_swt_packet_info.swt_payload_pkt_bitsize = 32;
        *(uint32_t *)m_payload_buffer = m_curr_packet_in->getD32Val();
        break;


    case STM_PKT_D64:        /**< 64 bit data payload packet */
        m_swt_packet_info.swt_payload_pkt_bitsize = 64;
        *(uint64_t *)m_payload_buffer = m_curr_packet_in->getD64Val();
        break;
    }
    m_output_elem.setExtendedDataPtr(m_payload_buffer);
    if (m_curr_packet_in->isMarkerPkt())
        m_swt_packet_info.swt_marker_packet = 1;

}

/* End of File trc_pkt_decode_stm.cpp */
