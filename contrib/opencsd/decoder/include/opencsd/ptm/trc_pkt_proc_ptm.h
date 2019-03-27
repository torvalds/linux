/*
 * \file       trc_pkt_proc_ptm.h
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

#ifndef ARM_TRC_PKT_PROC_PTM_H_INCLUDED
#define ARM_TRC_PKT_PROC_PTM_H_INCLUDED

#include "trc_pkt_types_ptm.h"
#include "common/trc_pkt_proc_base.h"
#include "trc_pkt_elem_ptm.h"
#include "trc_cmp_cfg_ptm.h"

class PtmTrcPacket;
class PtmConfig;

/** @addtogroup ocsd_pkt_proc
@{*/



class TrcPktProcPtm : public TrcPktProcBase< PtmTrcPacket,  ocsd_ptm_pkt_type, PtmConfig>
{
public:
    TrcPktProcPtm();
    TrcPktProcPtm(int instIDNum);
    virtual ~TrcPktProcPtm();

protected:
    /* implementation packet processing interface */
    virtual ocsd_datapath_resp_t processData(  const ocsd_trc_index_t index,
                                                const uint32_t dataBlockSize,
                                                const uint8_t *pDataBlock,
                                                uint32_t *numBytesProcessed);
    virtual ocsd_datapath_resp_t onEOT();
    virtual ocsd_datapath_resp_t onReset();
    virtual ocsd_datapath_resp_t onFlush();
    virtual ocsd_err_t onProtocolConfig();
    virtual const bool isBadPacket() const;

    void InitPacketState();      // clear current packet state.
    void InitProcessorState();   // clear all previous process state

    ocsd_datapath_resp_t outputPacket();

    typedef enum _process_state {
        WAIT_SYNC,
        PROC_HDR,
        PROC_DATA,
        SEND_PKT, 
    } process_state;
    
    process_state m_process_state;  // process algorithm state.

    std::vector<uint8_t> m_currPacketData;  // raw data
    uint32_t m_currPktIdx;   // index into packet when expanding
    PtmTrcPacket m_curr_packet;  // expanded packet
    ocsd_trc_index_t m_curr_pkt_index; // trace index at start of packet.
    
    const bool readByte(uint8_t &currByte);
    const bool readByte(); // just read into buffer, don't need the value
    void unReadByte();  // remove last byte from the buffer.

    uint8_t m_chanIDCopy;

    // current data block being processed.
    const uint8_t *m_pDataIn;
    uint32_t m_dataInLen;
    uint32_t m_dataInProcessed;
    ocsd_trc_index_t m_block_idx; // index start for current block

    // processor synchronisation
    const bool isSync() const;
    ocsd_datapath_resp_t waitASync();       //!< look for first synchronisation point in the packet stream
    bool m_waitASyncSOPkt;
    bool m_bAsyncRawOp;
    bool m_bOPNotSyncPkt;                   //!< true if output not sync packet when waiting for ASYNC

    // ** packet processing functions.
    void pktASync();
    void pktISync();
    void pktTrigger();
    void pktWPointUpdate();
    void pktIgnore();
    void pktCtxtID();
    void pktVMID();
    void pktAtom();
    void pktTimeStamp();
    void pktExceptionRet();
    void pktBranchAddr();
    void pktReserved();

    // async finder
    typedef enum _async_result {
        ASYNC,      //!< pattern confirmed async 0x00 x 5, 0x80
        NOT_ASYNC,  //!< pattern confirmed not async
        ASYNC_EXTRA_0,  //!< pattern confirmed 0x00 x N + ASYNC
        THROW_0,    //!< long pattern of 0x00 - throw some away.
        ASYNC_INCOMPLETE, //!< not enough input data.
    } async_result_t;

    async_result_t findAsync();

    int m_async_0;  // number of current consecutive async 0s

    bool m_part_async;

    // number of extra 0s before we throw 0 on async detect.
    #define ASYNC_PAD_0_LIMIT 11
    // number of 0s minimum to form an async
    #define ASYNC_REQ_0 5

    // extraction sub-routines
    void extractCtxtID(int idx, uint32_t &ctxtID);
    void extractCycleCount(int idx, uint32_t &cycleCount);
    int extractTS(uint64_t &tsVal, uint8_t &tsUpdateBits);
    uint32_t extractAddress(const int offset,uint8_t &total_bits);

    // number of bytes required for a complete packet - used in some multi byte packets
    int m_numPktBytesReq;

    // packet processing state
    bool m_needCycleCount;
    bool m_gotCycleCount;
    int m_gotCCBytes;           // number of CC bytes read so far

    int m_numCtxtIDBytes;
    int m_gotCtxtIDBytes;

    bool m_gotTSBytes;      //!< got all TS bytes
    int m_tsByteMax;        //!< max size for TS portion of TS packet.

    // branch address state
    bool m_gotAddrBytes;    //!< got all Addr bytes in branch packet
    int m_numAddrBytes;     //!< number of address bytes
    bool m_gotExcepBytes;   //!< got all needed exception bytes
    int m_numExcepBytes;          //!< got 1st exception byte
    ocsd_isa m_addrPktIsa; //!< ISA of the branch address packet
    int m_excepAltISA;      //!< Alt ISA bit iff exception bytes

    // bad packets 
    void throwMalformedPacketErr(const char *pszErrMsg);
    void throwPacketHeaderErr(const char *pszErrMsg);


    // packet processing function table
    typedef void (TrcPktProcPtm::*PPKTFN)(void);
    PPKTFN m_pIPktFn;

    struct _pkt_i_table_t {
        ocsd_ptm_pkt_type pkt_type;
        PPKTFN pptkFn;
    } m_i_table[256];

    void BuildIPacketTable();    

};

inline const bool TrcPktProcPtm::isSync() const
{
    return (bool)(m_curr_packet.getType() == PTM_PKT_NOTSYNC);
}

inline void TrcPktProcPtm::throwMalformedPacketErr(const char *pszErrMsg)
{
    m_curr_packet.SetErrType(PTM_PKT_BAD_SEQUENCE);
    throw ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_BAD_PACKET_SEQ,m_curr_pkt_index,m_chanIDCopy,pszErrMsg);
}

inline void TrcPktProcPtm::throwPacketHeaderErr(const char *pszErrMsg)
{
    throw ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_INVALID_PCKT_HDR,m_curr_pkt_index,m_chanIDCopy,pszErrMsg);
}

inline const bool TrcPktProcPtm::readByte()
{
    uint8_t currByte;
    return readByte(currByte);    
}

/** @}*/

#endif // ARM_TRC_PKT_PROC_PTM_H_INCLUDED

/* End of File trc_pkt_proc_ptm.h */
