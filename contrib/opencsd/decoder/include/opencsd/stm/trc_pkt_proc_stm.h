/*
 * \file       trc_pkt_proc_stm.h
 * \brief      OpenCSD : STM packet processing
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

#ifndef ARM_TRC_PKT_PROC_STM_H_INCLUDED
#define ARM_TRC_PKT_PROC_STM_H_INCLUDED

#include <vector>

#include "trc_pkt_types_stm.h"
#include "common/trc_pkt_proc_base.h"
#include "trc_pkt_elem_stm.h"
#include "trc_cmp_cfg_stm.h"

/** @addtogroup ocsd_pkt_proc
@{*/

class TrcPktProcStm : public TrcPktProcBase<StmTrcPacket, ocsd_stm_pkt_type, STMConfig>
{
public:
    TrcPktProcStm();
    TrcPktProcStm(int instIDNum);
    virtual ~TrcPktProcStm();

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


    typedef enum _process_state {
        WAIT_SYNC,
        PROC_HDR,
        PROC_DATA,
        SEND_PKT
    } process_state;

    process_state m_proc_state;

private:

    void initObj();
    void initProcessorState();
    void initNextPacket();
    void waitForSync(const ocsd_trc_index_t blk_st_index);

    ocsd_datapath_resp_t outputPacket();   //!< send packet on output 
    void sendPacket();                      //!< mark packet for send.
    void setProcUnsynced();                 //!< set processor state to unsynced
    void throwBadSequenceError(const char *pszMessage = "");
    void throwReservedHdrError(const char *pszMessage = "");

    // packet processing routines
    // 1 nibble opcodes
    void stmPktReserved();
    void stmPktNull();
    void stmPktM8();
    void stmPktMERR();
    void stmPktC8();
    void stmPktD4();
    void stmPktD8();
    void stmPktD16();
    void stmPktD32();
    void stmPktD64();
    void stmPktD4MTS();
    void stmPktD8MTS();
    void stmPktD16MTS();
    void stmPktD32MTS();
    void stmPktD64MTS();
    void stmPktFlagTS();
    void stmPktFExt();

    // 2 nibble opcodes 0xFn
    void stmPktReservedFn();
    void stmPktF0Ext();
    void stmPktGERR();
    void stmPktC16();
    void stmPktD4TS();
    void stmPktD8TS();
    void stmPktD16TS();
    void stmPktD32TS();
    void stmPktD64TS();
    void stmPktD4M();
    void stmPktD8M();
    void stmPktD16M();
    void stmPktD32M();
    void stmPktD64M();
    void stmPktFlag();
    void stmPktASync();

    // 3 nibble opcodes 0xF0n
    void stmPktReservedF0n();
    void stmPktVersion();
    void stmPktNullTS();
    void stmPktTrigger();
    void stmPktTriggerTS();
    void stmPktFreq();
    
    void stmExtractTS(); // extract a TS in packets that require it.
    void stmExtractVal8(uint8_t nibbles_to_val);
    void stmExtractVal16(uint8_t nibbles_to_val);
    void stmExtractVal32(uint8_t nibbles_to_val);
    void stmExtractVal64(uint8_t nibbles_to_val);

    uint64_t bin_to_gray(uint64_t bin_value);
    uint64_t gray_to_bin(uint64_t gray_value);
	void pktNeedsTS();	// init the TS extraction routines

    // data processing op function tables
    void buildOpTables();

    typedef void (TrcPktProcStm::*PPKTFN)(void);
    PPKTFN m_pCurrPktFn;    // current active processing function.

    PPKTFN m_1N_ops[0x10];
    PPKTFN m_2N_ops[0x10];
    PPKTFN m_3N_ops[0x10];

    // read a nibble from the input data - may read a byte and set spare or return spare.
    // handles setting up packet data block and end of input 
    bool readNibble();

    const bool dataToProcess() const;       //!< true if data to process, or packet to send

    void savePacketByte(const uint8_t val);       //!< save data to packet buffer if we need it for monitor.

    // packet data 
    StmTrcPacket m_curr_packet;     //!< current packet.
    bool m_bNeedsTS;                //!< packet requires a TS
    bool m_bIsMarker;

    
    bool m_bStreamSync;             //!< packet stream is synced

    // input data handling
    uint8_t  m_num_nibbles;                 //!< number of nibbles in the current packet
    uint8_t  m_nibble;                      //!< current nibble being processed.
    uint8_t  m_nibble_2nd;                  //!< 2nd unused nibble from a processed byte.
    bool     m_nibble_2nd_valid;            //!< 2nd nibble is valid;
    uint8_t  m_num_data_nibbles;            //!< number of nibbles needed to acheive payload.

    const uint8_t *m_p_data_in;             //!< pointer to input data.
    uint32_t  m_data_in_size;               //!< amount of data in.
    uint32_t  m_data_in_used;               //!< amount of data processed.
    ocsd_trc_index_t m_packet_index;       //!< byte index for start of current packet

    std::vector<uint8_t> m_packet_data;     //!< current packet data (bytes) - only saved if needed to output to monitor.
    bool m_bWaitSyncSaveSuppressed;         //!< don't save byte at a time when waitsync

    // payload data
    uint8_t   m_val8;                       //!< 8 bit payload.
    uint16_t  m_val16;                      //!< 16 bit payload
    uint32_t  m_val32;                      //!< 32 bit payload
    uint64_t  m_val64;                      //!< 64 bit payload

    // timestamp handling
	uint8_t   m_req_ts_nibbles;	
	uint8_t   m_curr_ts_nibbles;
	uint64_t  m_ts_update_value;
	bool m_ts_req_set;


    // sync handling - need to spot sync mid other packet in case of wrap / discontinuity
    uint8_t   m_num_F_nibbles;              //!< count consecutive F nibbles.
    bool m_sync_start;                      //!< possible start of sync
    bool m_is_sync;                         //!< true if found sync at current nibble
    ocsd_trc_index_t m_sync_index;         //!< index of start of possible sync packet

    void checkSyncNibble();                 //!< check current nibble as part of sync.
    void clearSyncCount();                  //!< valid packet, so clear sync counters (i.e. a trailing ffff is not part of sync).

    class monAttachNotify : public IComponentAttachNotifier
    {
    public:
        monAttachNotify() { m_bInUse = false; };
        virtual ~monAttachNotify() {};
        virtual void attachNotify(const int num_attached) { m_bInUse = (num_attached > 0); };
        
        const bool usingMonitor() const { return m_bInUse; };

    private:
        bool m_bInUse;
    } mon_in_use;
};

inline const bool TrcPktProcStm::dataToProcess() const
{
    // data to process if
    // 1) not processed all the input bytes
    // 2) there is still a nibble available from the last byte.
    // 3) bytes processed, but there is a full packet to send
    return (m_data_in_used < m_data_in_size) || m_nibble_2nd_valid || (m_proc_state == SEND_PKT);
}


inline void TrcPktProcStm::checkSyncNibble()
{
    if(m_nibble != 0xF)
    {
        if(!m_sync_start)
            return;

        if((m_nibble == 0) && (m_num_F_nibbles >= 21))
        {
            m_is_sync = true;   //this nibble marks a sync sequence - keep the F nibble count
        }
        else
        {            
            clearSyncCount();   // clear all sync counters
        }
        return;
    }

    m_num_F_nibbles++;
    if(!m_sync_start)
    {
        m_sync_start = true;
        m_sync_index = m_packet_index + ((m_num_nibbles - 1) / 2);
    }
}

inline void TrcPktProcStm::clearSyncCount()
{
    m_num_F_nibbles = 0;
    m_sync_start = false;
    m_is_sync = false;
}

inline void TrcPktProcStm::sendPacket()
{
    m_proc_state = SEND_PKT;
}

inline void TrcPktProcStm::setProcUnsynced()
{
    m_proc_state = WAIT_SYNC;
    m_bStreamSync = false;
}


inline void TrcPktProcStm::savePacketByte(const uint8_t val)
{
    // save packet data if using monitor and synchronised.
    if(mon_in_use.usingMonitor() && !m_bWaitSyncSaveSuppressed)
        m_packet_data.push_back(val);
}

/** @}*/

#endif // ARM_TRC_PKT_PROC_STM_H_INCLUDED

/* End of File trc_pkt_proc_stm.h */
