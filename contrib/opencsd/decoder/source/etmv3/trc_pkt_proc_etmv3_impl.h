/*
 * \file       trc_pkt_proc_etmv3_impl.h
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

#ifndef ARM_TRC_PKT_PROC_ETMV3_IMPL_H_INCLUDED
#define ARM_TRC_PKT_PROC_ETMV3_IMPL_H_INCLUDED

#include "opencsd/etmv3/trc_pkt_proc_etmv3.h"
#include "opencsd/etmv3/trc_cmp_cfg_etmv3.h"
#include "opencsd/etmv3/trc_pkt_elem_etmv3.h"

#define MAX_PACKET_SIZE 32
#define ASYNC_SIZE 6

class EtmV3PktProcImpl
{
public:
    EtmV3PktProcImpl();
    ~EtmV3PktProcImpl();

    void Initialise(TrcPktProcEtmV3 *p_interface);

    ocsd_err_t Configure(const EtmV3Config *p_config);


    ocsd_datapath_resp_t processData(  const ocsd_trc_index_t index,
                                        const uint32_t dataBlockSize,
                                        const uint8_t *pDataBlock,
                                        uint32_t *numBytesProcessed);
    ocsd_datapath_resp_t onEOT();
    ocsd_datapath_resp_t onReset();
    ocsd_datapath_resp_t onFlush();
    const bool isBadPacket() const;

protected:
    typedef enum _process_state {
        WAIT_SYNC,
        PROC_HDR,
        PROC_DATA,
        SEND_PKT, 
        PROC_ERR,
    } process_state;
    
    process_state m_process_state;
    

    void InitPacketState();      // clear current packet state.
    void InitProcessorState();   // clear all previous process state

    // byte processing

    uint32_t waitForSync(const uint32_t dataBlockSize, const uint8_t *pDataBlock);  //!< look for sync, return none-sync bytes processed.
    ocsd_err_t processHeaderByte(uint8_t by);
    ocsd_err_t processPayloadByte(uint8_t by);

    // packet handling - main routines
    void OnBranchAddress();
    void OnISyncPacket();
    uint32_t extractCtxtID();
    uint64_t extractTimestamp(uint8_t &tsBits);
    uint32_t extractDataAddress(uint8_t &bits, bool &updateBE, uint8_t &beVal);
    uint32_t extractDataValue(const int dataByteSize);
    uint32_t extractCycleCount();

    // packet handling - helper routines
    uint32_t extractBrAddrPkt(int &nBitsOut);
    void extractExceptionData();
    void checkPktLimits();
    void setBytesPartPkt(const int numBytes, const process_state nextState, const ocsd_etmv3_pkt_type nextType); // set first n bytes from current packet to be sent via alt packet.

    // packet output
    void SendPacket();  // mark state for packet output
    ocsd_datapath_resp_t outputPacket();   // output a packet

    // bad packets 
    void throwMalformedPacketErr(const char *pszErrMsg);
    void throwPacketHeaderErr(const char *pszErrMsg);
    void throwUnsupportedErr(const char *pszErrMsg);

    uint32_t m_bytesProcessed; // bytes processed by the process data routine (index into input buffer)
    std::vector<uint8_t> m_currPacketData;  // raw data
    uint32_t m_currPktIdx;   // index into packet when expanding
    EtmV3TrcPacket m_curr_packet;  // expanded packet

    std::vector<uint8_t> m_partPktData;   // raw data when we need to split a packet. 
    bool m_bSendPartPkt;                  // mark the part packet as the one we send.
    process_state m_post_part_pkt_state;  // state to set after part packet set
    ocsd_etmv3_pkt_type m_post_part_pkt_type;  // reset the packet type.

    // process state
    bool            m_bStreamSync;          //!< true if we have synced this stream
    bool            m_bStartOfSync;         //!< true if we have a start of sync.
    
    // packet state 
	uint32_t m_bytesExpectedThisPkt; // used by some of the variable packet length types.	
	bool m_BranchPktNeedsException;
	bool m_bIsync_got_cycle_cnt;
	bool m_bIsync_get_LSiP_addr;
	int m_IsyncInfoIdx;
	bool m_bExpectingDataAddress;
	bool m_bFoundDataAddress;

    ocsd_trc_index_t m_packet_index;   // index of the start of the current packet
    ocsd_trc_index_t m_packet_curr_byte_index; // index of the current byte.

    bool m_isInit;
    TrcPktProcEtmV3 *m_interface;       /**< The interface to the other decode components */
    
    EtmV3Config m_config;

    uint8_t m_chanIDCopy;
};


inline void EtmV3PktProcImpl::SendPacket()
{
    m_process_state = SEND_PKT;
}

inline void EtmV3PktProcImpl::throwMalformedPacketErr(const char *pszErrMsg)
{
    throw ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_BAD_PACKET_SEQ,m_packet_index,m_chanIDCopy,pszErrMsg);
}

inline void EtmV3PktProcImpl::throwPacketHeaderErr(const char *pszErrMsg)
{
    throw ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_INVALID_PCKT_HDR,m_packet_index,m_chanIDCopy,pszErrMsg);
}

inline void EtmV3PktProcImpl::throwUnsupportedErr(const char *pszErrMsg)
{
    throw ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_HW_CFG_UNSUPP,m_packet_index,m_chanIDCopy,pszErrMsg);
}


inline const bool EtmV3PktProcImpl::isBadPacket() const
{
    return m_curr_packet.isBadPacket();
}


#endif // ARM_TRC_PKT_PROC_ETMV3_IMPL_H_INCLUDED

/* End of File trc_pkt_proc_etmv3_impl.h */
