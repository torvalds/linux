/*
 * \file       trc_pkt_decode_stm.h
 * \brief      OpenCSD : STM packet decoder 
 *
 *  Convert the incoming indidvidual STM packets to 
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


#ifndef ARM_TRC_PKT_DECODE_STM_H_INCLUDED
#define ARM_TRC_PKT_DECODE_STM_H_INCLUDED


#include "common/trc_pkt_decode_base.h"
#include "opencsd/stm/trc_pkt_elem_stm.h"
#include "opencsd/stm/trc_cmp_cfg_stm.h"
#include "common/trc_gen_elem.h"

class TrcPktDecodeStm : public TrcPktDecodeBase<StmTrcPacket, STMConfig>
{
public:
    TrcPktDecodeStm();
    TrcPktDecodeStm(int instIDNum);
    virtual ~TrcPktDecodeStm();
    
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
    void initDecoder();
    void resetDecoder();
    void initPayloadBuffer();
    bool isInit() { return (bool)((m_config != 0) && (m_payload_buffer != 0)); };
    ocsd_datapath_resp_t decodePacket(bool &bPktDone);  //!< decode the current incoming packet
    void clearSWTPerPcktInfo();
    void updatePayload(bool &bSendPacket);

    typedef enum {
        NO_SYNC,        //!< pre start trace - init state or after reset or overflow, loss of sync.
        WAIT_SYNC,      //!< waiting for sync packet.
        DECODE_PKTS     //!< processing input packet.  
    } processor_state_t;

    processor_state_t m_curr_state;  

    ocsd_swt_info_t m_swt_packet_info;

    uint8_t *m_payload_buffer;  //!< payload buffer - allocated for one or multiple packets according to config
    int m_payload_size;         //!< payload buffer total size in bytes.
    int m_payload_used;         //!< payload buffer used in bytes - current payload size.
    bool m_payload_odd_nibble;  //!< last used byte in payload contains a single 4 bit packet.
    int m_num_pkt_correlation;  //!< number of identical payload packets to buffer up before output. - fixed at 1 till later update

    uint8_t m_CSID;             //!< Coresight trace ID for this decoder.

    bool m_decode_pass1;        //!< flag to indicate 1st pass of packet decode.



//** output element
    OcsdTraceElement m_output_elem; //!< output packet
};

#endif // ARM_TRC_PKT_DECODE_STM_H_INCLUDED

/* End of File trc_pkt_decode_stm.h */
