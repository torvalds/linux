/*
 * \file       trc_frame_decoder_impl.h
 * \brief      OpenCSD : Trace Deformatter implementation.
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

#ifndef ARM_TRC_FRAME_DECODER_IMPL_H_INCLUDED
#define ARM_TRC_FRAME_DECODER_IMPL_H_INCLUDED

#include "opencsd/ocsd_if_types.h"
#include "common/comp_attach_pt_t.h"
#include "interfaces/trc_data_raw_in_i.h"
#include "interfaces/trc_data_rawframe_in_i.h"
#include "interfaces/trc_indexer_src_i.h"
#include "common/trc_component.h"

//! output data fragment from the current frame - collates bytes associated with an ID.
typedef struct _out_chan_data {
    ocsd_trc_index_t index;      //!< trace source index for start of these bytes
    uint8_t id;             //!< Id for these bytes
    uint8_t data[15];       //!< frame data bytes for this ID 
    uint32_t valid;         //!< Valid data bytes. 
    uint32_t used;          //!< Data bytes output (used by attached processor).
} out_chan_data;

class TraceFmtDcdImpl : public TraceComponent, ITrcDataIn
{
private:
    TraceFmtDcdImpl();
    TraceFmtDcdImpl(int instNum);
    virtual ~TraceFmtDcdImpl();

    /* the data input interface from the reader */
    virtual ocsd_datapath_resp_t TraceDataIn(  const ocsd_datapath_op_t op, 
                                                const ocsd_trc_index_t index, 
                                                const uint32_t dataBlockSize, 
                                                const uint8_t *pDataBlock, 
                                                uint32_t *numBytesProcessed);

    /* enable / disable ID streams - default as all enabled */
    ocsd_err_t OutputFilterIDs(std::vector<uint8_t> &id_list, bool bEnable);
    ocsd_err_t OutputFilterAllIDs(bool bEnable);

    /* decode control */
    ocsd_datapath_resp_t Reset();    /* reset the decode to the start state, drop partial data - propogate to attached components */
    ocsd_datapath_resp_t Flush(); 
    ocsd_err_t DecodeConfigure(uint32_t flags);
    ocsd_err_t SetForcedSyncIndex(ocsd_trc_index_t index, bool bSet);

private:
    ocsd_datapath_resp_t executeNoneDataOpAllIDs(ocsd_datapath_op_t op, const ocsd_trc_index_t index = 0);
    ocsd_datapath_resp_t processTraceData(const ocsd_trc_index_t index, 
                                                const uint32_t dataBlockSize, 
                                                const uint8_t *pDataBlock, 
                                                uint32_t *numBytesProcessed);
    // process phases
    bool checkForSync(); // find the sync point in the incoming block
    bool extractFrame(); // extract the frame data from incoming stream
    bool unpackFrame(); // process a complete frame.
    bool outputFrame(); // output data to channels.


    // managing data path responses.
    void InitCollateDataPathResp() { m_highestResp = OCSD_RESP_CONT; };
    void CollateDataPathResp(const ocsd_datapath_resp_t resp);
    const ocsd_datapath_resp_t highestDataPathResp() const { return m_highestResp; };
    const bool dataPathCont() const { return (bool)(m_highestResp < OCSD_RESP_WAIT); };

    // deformat state 
    void resetStateParams();
    
    // synchronisation 
    uint32_t findfirstFSync();
    void outputUnsyncedBytes(uint32_t num_bytes);  // output bytes as unsynced from current buffer position.

    // output bytes to raw frame monitor
    void outputRawMonBytes(const ocsd_datapath_op_t op, 
                           const ocsd_trc_index_t index, 
                           const ocsd_rawframe_elem_t frame_element, 
                           const int dataBlockSize, 
                           const uint8_t *pDataBlock,
                           const uint8_t traceID);


    void setRawChanFilterAll(bool bEnable);
    const bool rawChanEnabled(const uint8_t id) const;

	int checkForResetFSyncPatterns();

    friend class TraceFormatterFrameDecoder;

    // attachment points

    componentAttachPt<ITrcDataIn> m_IDStreams[128];
    componentAttachPt<ITrcRawFrameIn> m_RawTraceFrame;

    componentAttachPt<ITrcSrcIndexCreator> m_SrcIndexer;


    ocsd_datapath_resp_t m_highestResp;
    
    /* static configuration */
    uint32_t m_cfgFlags;        /* configuration flags */
    ocsd_trc_index_t m_force_sync_idx;  
    bool m_use_force_sync;
    uint32_t m_alignment;
    
    /* dynamic state */
    ocsd_trc_index_t m_trc_curr_idx;    /* index of current trace data */
    bool m_frame_synced;
    bool m_first_data;
    uint8_t m_curr_src_ID;

    // incoming frame buffer 
    uint8_t m_ex_frm_data[OCSD_DFRMTR_FRAME_SIZE]; // buffer the current frame in case we have to stop part way through
    int m_ex_frm_n_bytes;   // number of valid bytes in the current frame (extraction)
    ocsd_trc_index_t m_trc_curr_idx_sof; // trace source index at start of frame.

    // channel output data - can never be more than a frame of data for a single ID.
    out_chan_data m_out_data[7];  // can only be 8 ID changes in a frame, but last on has no associated data so 7 possible data blocks
    int m_out_data_idx;          // number of out_chan_data frames used.
    int m_out_processed;          // number of complete out_chan_data frames output.
    
    /* local copy of input buffer pointers*/
    const uint8_t *m_in_block_base;
    uint32_t m_in_block_size;
    uint32_t m_in_block_processed;

    /* raw output options */
    bool m_b_output_packed_raw;
    bool m_b_output_unpacked_raw;

    bool m_raw_chan_enable[128];
};


#endif // ARM_TRC_FRAME_DECODER_IMPL_H_INCLUDED

/* End of File trc_frame_decoder_impl.h */
