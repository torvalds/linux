/*
 * \file       trc_frame_deformatter.cpp
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
#include <cstring>

#include "common/trc_frame_deformatter.h"
#include "trc_frame_deformatter_impl.h"

/***************************************************************/
/* Implementation */
/***************************************************************/

#ifdef __GNUC__
// G++ doesn't like the ## pasting
#define DEFORMATTER_NAME "DFMT_CSFRAMES"
#else
// VC is fine
#define DEFORMATTER_NAME OCSD_CMPNAME_PREFIX_FRAMEDEFORMATTER##"_CSFRAMES"
#endif

TraceFmtDcdImpl::TraceFmtDcdImpl() : TraceComponent(DEFORMATTER_NAME),
    m_cfgFlags(0),
    m_force_sync_idx(0),
    m_use_force_sync(false),
    m_alignment(16), // assume frame aligned data as default.
    m_b_output_packed_raw(false),
    m_b_output_unpacked_raw(false)

{
    resetStateParams();
    setRawChanFilterAll(true);
}

TraceFmtDcdImpl::TraceFmtDcdImpl(int instNum) : TraceComponent(DEFORMATTER_NAME, instNum),
    m_cfgFlags(0),
    m_force_sync_idx(0),
    m_use_force_sync(false),
    m_alignment(16)
{
    resetStateParams();
    setRawChanFilterAll(true);
}

TraceFmtDcdImpl::~TraceFmtDcdImpl()
{
}

ocsd_datapath_resp_t TraceFmtDcdImpl::TraceDataIn(
    const ocsd_datapath_op_t op, 
    const ocsd_trc_index_t index, 
    const uint32_t dataBlockSize, 
    const uint8_t *pDataBlock, 
    uint32_t *numBytesProcessed)
{
    ocsd_datapath_resp_t resp = OCSD_RESP_FATAL_INVALID_OP;
    InitCollateDataPathResp();

    m_b_output_packed_raw = m_RawTraceFrame.num_attached() && ((m_cfgFlags & OCSD_DFRMTR_PACKED_RAW_OUT) != 0);
    m_b_output_unpacked_raw = m_RawTraceFrame.num_attached() && ((m_cfgFlags & OCSD_DFRMTR_UNPACKED_RAW_OUT) != 0);

    switch(op)
    {
    case OCSD_OP_RESET: 
        resp = Reset();
        break;

    case OCSD_OP_FLUSH:
        resp = Flush();
        break;

    case OCSD_OP_EOT:
        // local 'flush' here?
        // pass on EOT to connected ID streams
        resp = executeNoneDataOpAllIDs(OCSD_OP_EOT);
        break;

    case OCSD_OP_DATA:
        if((dataBlockSize <= 0) || ( pDataBlock == 0) || (numBytesProcessed == 0))
            resp = OCSD_RESP_FATAL_INVALID_PARAM;
        else
            resp = processTraceData(index,dataBlockSize, pDataBlock, numBytesProcessed);
        break;

    default:
        break;
    }

    return resp;
}

/* enable / disable ID streams - default as all enabled */
ocsd_err_t TraceFmtDcdImpl::OutputFilterIDs(std::vector<uint8_t> &id_list, bool bEnable)
{
    ocsd_err_t err =  OCSD_OK;
    std::vector<uint8_t>::iterator iter = id_list.begin();
    uint8_t id = 0;

    while((iter < id_list.end()) && (err == OCSD_OK))
    {
        id = *iter;
        if(id > 128)
            err = OCSD_ERR_INVALID_ID;
        else
        {
            m_IDStreams[id].set_enabled(bEnable);
            m_raw_chan_enable[id] = bEnable;
        }
        iter++;
    }
    return err;
}

ocsd_err_t TraceFmtDcdImpl::OutputFilterAllIDs(bool bEnable)
{
    for(uint8_t id = 0; id < 128; id++)
    {
        m_IDStreams[id].set_enabled(bEnable);
    }
    setRawChanFilterAll(bEnable);
    return OCSD_OK;
}

void TraceFmtDcdImpl::setRawChanFilterAll(bool bEnable)
{
    for(int i=0; i<128; i++) 
    {
        m_raw_chan_enable[i] = bEnable;
    }
}

const bool TraceFmtDcdImpl::rawChanEnabled(const uint8_t id) const
{
    if(id < 128)
        return m_raw_chan_enable[id];
    return false;
}

/* decode control */
ocsd_datapath_resp_t TraceFmtDcdImpl::Reset()
{
    resetStateParams();
    InitCollateDataPathResp();
    return executeNoneDataOpAllIDs(OCSD_OP_RESET);
}

ocsd_datapath_resp_t TraceFmtDcdImpl::Flush()
{
    executeNoneDataOpAllIDs(OCSD_OP_FLUSH);    // flush any upstream data.
    if(dataPathCont())
        outputFrame();  // try to flush any partial frame data remaining
    return highestDataPathResp();
}

ocsd_datapath_resp_t TraceFmtDcdImpl::executeNoneDataOpAllIDs(ocsd_datapath_op_t op,
                                                              const ocsd_trc_index_t index /* = 0*/)
{
    ITrcDataIn *pTrcComp = 0;
    for(uint8_t id = 0; id < 128; id++)
    {
        if(m_IDStreams[id].num_attached())
        {
            pTrcComp = m_IDStreams[id].first();
            while(pTrcComp)
            {
                CollateDataPathResp(pTrcComp->TraceDataIn(op,index,0,0,0));
                pTrcComp = m_IDStreams[id].next();
            }
        }
    }

    if( m_RawTraceFrame.num_attached())
    {
        if(m_RawTraceFrame.first())
            m_RawTraceFrame.first()->TraceRawFrameIn(op,0,OCSD_FRM_NONE,0,0,0);
    }
    return highestDataPathResp();
}

void TraceFmtDcdImpl::outputRawMonBytes(const ocsd_datapath_op_t op, 
                           const ocsd_trc_index_t index, 
                           const ocsd_rawframe_elem_t frame_element, 
                           const int dataBlockSize, 
                           const uint8_t *pDataBlock,
                           const uint8_t traceID)                           
{
    if( m_RawTraceFrame.num_attached())
    {
        if(m_RawTraceFrame.first())
            m_RawTraceFrame.first()->TraceRawFrameIn(op,index,frame_element,dataBlockSize, pDataBlock,traceID);
    }
}

void TraceFmtDcdImpl::CollateDataPathResp(const ocsd_datapath_resp_t resp)
{
    // simple most severe error across multiple IDs.
    if(resp > m_highestResp) m_highestResp = resp;
}

ocsd_datapath_resp_t TraceFmtDcdImpl::processTraceData( 
                                        const ocsd_trc_index_t index, 
                                        const uint32_t dataBlockSize, 
                                        const uint8_t *pDataBlock, 
                                        uint32_t *numBytesProcessed
                                        )
{
    try {

        if(!m_first_data)  // is this the initial data block?
        {
            m_trc_curr_idx = index;
        }
        else
        {
            if(m_trc_curr_idx != index) // none continuous trace data - throw an error.
                throw ocsdError(OCSD_ERR_SEV_ERROR,OCSD_ERR_DFMTR_NOTCONTTRACE,index);
        }
        
        if(dataBlockSize % m_alignment) // must be correctly aligned data 
        {
            ocsdError err(OCSD_ERR_SEV_ERROR, OCSD_ERR_INVALID_PARAM_VAL);
            char msg_buffer[64];
            sprintf(msg_buffer,"Input block incorrect size, must be %d byte multiple", m_alignment);
            err.setMessage(msg_buffer);
            throw ocsdError(&err);
        }

        // record the incoming block for extraction routines to use.
        m_in_block_base = pDataBlock;
        m_in_block_size = dataBlockSize;
        m_in_block_processed = 0;

        // processing loop...
        if(checkForSync())
        {
            bool bProcessing = true;
            while(bProcessing) 
            {
                bProcessing = extractFrame();   // will stop on end of input data.
                if(bProcessing)
                    bProcessing = unpackFrame();
                if(bProcessing)
                    bProcessing = outputFrame(); // will stop on data path halt.
            }
        }
    }
    catch(const ocsdError &err) {
        LogError(err);
        CollateDataPathResp(OCSD_RESP_FATAL_INVALID_DATA);
    }
    catch(...) {
        LogError(ocsdError(OCSD_ERR_SEV_ERROR, OCSD_ERR_FAIL));
        CollateDataPathResp(OCSD_RESP_FATAL_SYS_ERR);
    }

    if(!m_first_data)
        m_first_data = true;
    
    // update the outputs.
    *numBytesProcessed = m_in_block_processed;

    return highestDataPathResp();
}

ocsd_err_t  TraceFmtDcdImpl::DecodeConfigure(uint32_t flags)
{
    const char *pszErrMsg = "";
    ocsd_err_t err = OCSD_OK;

    if((flags & ~OCSD_DFRMTR_VALID_MASK) != 0)
    {
        err = OCSD_ERR_INVALID_PARAM_VAL;
        pszErrMsg = "Unknown Config Flags";
    }

    if((flags & OCSD_DFRMTR_VALID_MASK) == 0)
    {
        err = OCSD_ERR_INVALID_PARAM_VAL;
        pszErrMsg = "No Config Flags Set";
    }

    if((flags & (OCSD_DFRMTR_HAS_FSYNCS | OCSD_DFRMTR_HAS_HSYNCS)) &&
       (flags & OCSD_DFRMTR_FRAME_MEM_ALIGN)
       )
    {
        err = OCSD_ERR_INVALID_PARAM_VAL;
        pszErrMsg = "Invalid Config Flag Combination Set";
    }

    if(err != OCSD_OK)
    {
        ocsdError errObj(OCSD_ERR_SEV_ERROR,OCSD_ERR_INVALID_PARAM_VAL);
        errObj.setMessage(pszErrMsg);
        LogError(errObj);
    }
    else
    {
        m_cfgFlags = flags;
        m_alignment = 16;
        if(flags & OCSD_DFRMTR_HAS_FSYNCS)
            m_alignment = 4;
        else if(flags & OCSD_DFRMTR_HAS_HSYNCS)
            m_alignment = 2;
    }
    return err;
}

void TraceFmtDcdImpl::resetStateParams()
{
    // overall dynamic state - intra frame
    m_trc_curr_idx = OCSD_BAD_TRC_INDEX;    /* source index of current trace data */
    m_frame_synced = false;
    m_first_data = false;    
    m_curr_src_ID = OCSD_BAD_CS_SRC_ID;

    // current frame processing
    m_ex_frm_n_bytes = 0;
    m_trc_curr_idx_sof = OCSD_BAD_TRC_INDEX;
}

bool TraceFmtDcdImpl::checkForSync()
{
    // we can sync on:-
    // 16 byte alignment - standard input buffers such as ETB
    // FSYNC packets in the stream
    // forced index programmed into the object.
    uint32_t unsynced_bytes = 0;

    if(!m_frame_synced)
    {
        if(m_use_force_sync)
        {
            // is the force sync point in this block?
            if((m_force_sync_idx >= m_trc_curr_idx) && (m_force_sync_idx < (m_trc_curr_idx + m_in_block_size)))
            {
                unsynced_bytes = m_force_sync_idx - m_trc_curr_idx;
                m_frame_synced = true;
            }
            else
            {
                unsynced_bytes = m_in_block_size;
            }
        }
        else if( m_cfgFlags & OCSD_DFRMTR_HAS_FSYNCS)   // memory aligned data
        {
             unsynced_bytes = findfirstFSync();

        }
        else
        {
            // OCSD_DFRMTR_FRAME_MEM_ALIGN - this has guaranteed 16 byte frame size and alignment.
            m_frame_synced = true;
        }

        if(unsynced_bytes)
        {
            outputUnsyncedBytes(unsynced_bytes);
            m_in_block_processed = unsynced_bytes;
            m_trc_curr_idx += unsynced_bytes;
        }
    }
    return m_frame_synced;
}

uint32_t TraceFmtDcdImpl::findfirstFSync()
{
    uint32_t unsynced = m_in_block_size;  // consider entire block as unsynced at present.
    //**TBD - handle fsync patterns in TPIU captured code
    return unsynced;
}

void TraceFmtDcdImpl::outputUnsyncedBytes(uint32_t /*num_bytes*/)
{
    //**TBD:
}

int TraceFmtDcdImpl::checkForResetFSyncPatterns()
{
	const uint32_t FSYNC_PATTERN = 0x7FFFFFFF;    // LE host pattern for FSYNC	 
	bool check_for_fsync = true;
	int num_fsyncs = 0;
	const uint8_t *dataPtr = m_in_block_base + m_in_block_processed;

	while (check_for_fsync && (m_in_block_processed < m_in_block_size))
	{
		// look for consecutive fsyncs as padding or for reset downstream - both cases will reset downstream....
		if (*((uint32_t *)(dataPtr)) == FSYNC_PATTERN)
		{
			dataPtr += sizeof(uint32_t);
			num_fsyncs++;		
		}
		else
			check_for_fsync = false;
	}

	if (num_fsyncs)
	{
        printf("Frame deformatter: Found %d FSYNCS\n",num_fsyncs);
		if ((num_fsyncs % 4) == 0)
        {
            // reset the upstream decoders            
			executeNoneDataOpAllIDs(OCSD_OP_RESET,m_trc_curr_idx);
            
            // reset the intra frame parameters
            m_curr_src_ID = OCSD_BAD_CS_SRC_ID;
            m_ex_frm_n_bytes = 0;
            m_trc_curr_idx_sof = OCSD_BAD_TRC_INDEX;
        }
		else
		{
			// TBD: throw processing error, none frame size block of fsyncs
		}
	}
    return num_fsyncs * 4;
}


bool TraceFmtDcdImpl::extractFrame()
{
	const uint32_t FSYNC_PATTERN = 0x7FFFFFFF;    // LE host pattern for FSYNC	 
	const uint16_t HSYNC_PATTERN = 0x7FFF;        // LE host pattern for HSYNC
	
	
	bool cont_process = true;   // continue processing after extraction.
    uint32_t f_sync_bytes = 0; // skipped f sync bytes
    uint32_t h_sync_bytes = 0; // skipped h sync bytes
    uint32_t ex_bytes = 0;  // extracted bytes

    // memory aligned sources are always multiples of frames, aligned to start.
    if( m_cfgFlags & OCSD_DFRMTR_FRAME_MEM_ALIGN)
    {
		// some linux drivers (e.g. for perf) will insert FSYNCS to pad or differentiate
        // between blocks of aligned data, always in frame aligned complete 16 byte frames.       
        if (m_cfgFlags & OCSD_DFRMTR_RESET_ON_4X_FSYNC)
        {
            f_sync_bytes = checkForResetFSyncPatterns();

            /* in this case the FSYNC pattern is output on both packed and unpacked cases */
            if (f_sync_bytes && (m_b_output_packed_raw || m_b_output_unpacked_raw))
            {
                outputRawMonBytes(OCSD_OP_DATA,
                    m_trc_curr_idx,
                    OCSD_FRM_FSYNC,
                    f_sync_bytes,
                    m_in_block_base + m_in_block_processed,
                    0);
            }
        }

        if((m_in_block_processed+f_sync_bytes) == m_in_block_size)
        {
            m_ex_frm_n_bytes = 0;
            cont_process = false;   // end of input data.
        }
		else
		{
			// always a complete frame.
			m_ex_frm_n_bytes = OCSD_DFRMTR_FRAME_SIZE;
			memcpy(m_ex_frm_data, m_in_block_base + m_in_block_processed + f_sync_bytes, m_ex_frm_n_bytes);
			m_trc_curr_idx_sof = m_trc_curr_idx + f_sync_bytes;
			ex_bytes = OCSD_DFRMTR_FRAME_SIZE;
        }
    }
    else
    {
        // extract data accounting for frame syncs and hsyncs if present.
        // we know we are aligned at this point - could be FSYNC or HSYNCs here.

        // check what we a looking for
        bool hasFSyncs =  ((m_cfgFlags & OCSD_DFRMTR_HAS_FSYNCS) == OCSD_DFRMTR_HAS_FSYNCS);
        bool hasHSyncs =  ((m_cfgFlags & OCSD_DFRMTR_HAS_HSYNCS) == OCSD_DFRMTR_HAS_HSYNCS);

        const uint8_t *dataPtr = m_in_block_base+m_in_block_processed;
        const uint8_t *eodPtr = m_in_block_base+m_in_block_size;
        
        cont_process = (bool)(dataPtr < eodPtr);
        
        // can have FSYNCS at start of frame (in middle is an error).
        if(hasFSyncs && cont_process && (m_ex_frm_n_bytes == 0))
        {
            while((*((uint32_t *)(dataPtr)) == FSYNC_PATTERN) && cont_process)
            {
                f_sync_bytes += 4;
                dataPtr += 4;
                cont_process = (bool)(dataPtr < eodPtr);

                // TBD:  output raw FSYNC data on raw frame channel.
            }
        }

        // not an FSYNC
        while((m_ex_frm_n_bytes < OCSD_DFRMTR_FRAME_SIZE) && cont_process)
        {
            // check for illegal out of sequence FSYNC
            if((m_ex_frm_n_bytes % 4) == 0)
            {
                if(*((uint32_t *)(dataPtr)) == FSYNC_PATTERN) 
                {
                    // throw an illegal FSYNC error
                }
            }

            // mark start of frame after FSyncs 
            if(m_ex_frm_n_bytes == 0)
                m_trc_curr_idx_sof = m_trc_curr_idx + f_sync_bytes;

            m_ex_frm_data[m_ex_frm_n_bytes] = dataPtr[0];
            m_ex_frm_data[m_ex_frm_n_bytes+1] = dataPtr[1];
            m_ex_frm_n_bytes+=2;
            ex_bytes +=2;

            // check pair is not HSYNC
            if(*((uint16_t *)(dataPtr)) == HSYNC_PATTERN)
            {
                if(hasHSyncs)
                {
                    m_ex_frm_n_bytes-=2;
                    ex_bytes -= 2;
                    h_sync_bytes+=2;

                    // TBD:  output raw HSYNC data on raw frame channel.
                }
                else
                {
                    // throw illegal HSYNC error.
                }
            }

            dataPtr += 2;
            cont_process = (bool)(dataPtr < eodPtr);
        }

        // if we hit the end of data but still have a complete frame waiting, 
        // need to continue processing to allow it to be used.
        if(!cont_process && (m_ex_frm_n_bytes == OCSD_DFRMTR_FRAME_SIZE))
            cont_process = true;
    }

    // output raw data on raw frame channel - packed raw. 
    if ((m_ex_frm_n_bytes == OCSD_DFRMTR_FRAME_SIZE) && m_b_output_packed_raw)
    {
        outputRawMonBytes(  OCSD_OP_DATA, 
                            m_trc_curr_idx, 
                            OCSD_FRM_PACKED,
                            ex_bytes + f_sync_bytes + h_sync_bytes,
                            m_in_block_base+m_in_block_processed,
                            0);
    }

    // update the processed count for the buffer
    m_in_block_processed += m_ex_frm_n_bytes + f_sync_bytes + h_sync_bytes;

    // update index past the processed data   
    m_trc_curr_idx += m_ex_frm_n_bytes + f_sync_bytes + h_sync_bytes;

    return cont_process;
}

bool TraceFmtDcdImpl::unpackFrame()
{
    // unpack cannot fail as never called on incomplete frame.
    uint8_t frameFlagBit = 0x1;
    uint8_t newSrcID = OCSD_BAD_CS_SRC_ID;
    bool PrevIDandIDChange = false;

    // init output processing
    m_out_data_idx = 0;   
    m_out_processed = 0;

    // set up first out data packet...
    m_out_data[m_out_data_idx].id = m_curr_src_ID;
    m_out_data[m_out_data_idx].valid = 0;
    m_out_data[m_out_data_idx].index =  m_trc_curr_idx_sof;
    m_out_data[m_out_data_idx].used = 0;

    // work on byte pairs - bytes 0 - 13.
    for(int i = 0; i < 14; i+=2)
    {
        PrevIDandIDChange = false;

        // it's an ID + data
        if(m_ex_frm_data[i] & 0x1)
        {
            newSrcID = (m_ex_frm_data[i] >> 1) & 0x7f;
            if(newSrcID != m_curr_src_ID)   // ID change
            {
                PrevIDandIDChange = ((frameFlagBit & m_ex_frm_data[15]) != 0);

                // following byte for old id? 
                if(PrevIDandIDChange)
                    // 2nd byte always data
                    m_out_data[m_out_data_idx].data[m_out_data[m_out_data_idx].valid++] = m_ex_frm_data[i+1];

                // change ID
                m_curr_src_ID = newSrcID;

                // if we already have data in this buffer
                if(m_out_data[m_out_data_idx].valid > 0)
                {
                    m_out_data_idx++; // move to next buffer
                    m_out_data[m_out_data_idx].valid = 0;
                    m_out_data[m_out_data_idx].used = 0;
                    m_out_data[m_out_data_idx].index = m_trc_curr_idx_sof + i;
                }

                // set new ID on buffer
                m_out_data[m_out_data_idx].id = m_curr_src_ID;

                /// TBD - ID indexing in here.
            }
        }
        else
        // it's just data
        {
            m_out_data[m_out_data_idx].data[m_out_data[m_out_data_idx].valid++] = m_ex_frm_data[i] | ((frameFlagBit & m_ex_frm_data[15]) ? 0x1 : 0x0);             
        }

        // 2nd byte always data
        if(!PrevIDandIDChange) // output only if we didn't for an ID change + prev ID.
            m_out_data[m_out_data_idx].data[m_out_data[m_out_data_idx].valid++] = m_ex_frm_data[i+1];

        frameFlagBit <<= 1;
    }

    // unpack byte 14;

    // it's an ID
    if(m_ex_frm_data[14] & 0x1)
    {
        // no matter if change or not, no associated data in byte 15 anyway so just set.
        m_curr_src_ID = (m_ex_frm_data[14] >> 1) & 0x7f;
    }
    // it's data
    else
    {
        m_out_data[m_out_data_idx].data[m_out_data[m_out_data_idx].valid++] = m_ex_frm_data[14] | ((frameFlagBit & m_ex_frm_data[15]) ? 0x1 : 0x0); 
    }
    m_ex_frm_n_bytes = 0;   // mark frame as empty;
    return true;
}

// output data to channels.
bool TraceFmtDcdImpl::outputFrame()
{
    bool cont_processing = true;
    ITrcDataIn *pDataIn = 0;
    uint32_t bytes_used;

    // output each valid ID within the frame - stopping if we get a wait or error
    while((m_out_processed < (m_out_data_idx + 1)) && cont_processing)
    {
        
        // may have data prior to a valid ID appearing
        if(m_out_data[m_out_processed].id != OCSD_BAD_CS_SRC_ID)
        {
            if((pDataIn = m_IDStreams[m_out_data[m_out_processed].id].first()) != 0)
            {
                // log the stuff we are about to put out early so as to make it visible before interpretation
                // however, don't re-output if only part used first time round.
                if(m_b_output_unpacked_raw && (m_out_data[m_out_processed].used == 0) && rawChanEnabled( m_out_data[m_out_processed].id))
                {
                    outputRawMonBytes( OCSD_OP_DATA, 
                            m_out_data[m_out_processed].index, 
                            OCSD_FRM_ID_DATA,
                            m_out_data[m_out_processed].valid,
                            m_out_data[m_out_processed].data,
                            m_out_data[m_out_processed].id);
                }

                // output to the connected packet process
                CollateDataPathResp(pDataIn->TraceDataIn(OCSD_OP_DATA,
                    m_out_data[m_out_processed].index + m_out_data[m_out_processed].used,
                    m_out_data[m_out_processed].valid - m_out_data[m_out_processed].used,
                    m_out_data[m_out_processed].data + m_out_data[m_out_processed].used,
                    &bytes_used));               
                
                if(!dataPathCont())
                {
                    cont_processing = false;
                    m_out_data[m_out_processed].used += bytes_used;
                    if(m_out_data[m_out_processed].used == m_out_data[m_out_processed].valid)
                        m_out_processed++; // we have used up all this data.
                }
                else
                {
                    m_out_processed++; // we have sent this data;
                }
            }
            else
            {
                // optional raw output for debugging / monitor tools
                if(m_b_output_unpacked_raw && rawChanEnabled( m_out_data[m_out_processed].id))
                {
                    outputRawMonBytes(  OCSD_OP_DATA, 
                        m_out_data[m_out_processed].index, 
                        OCSD_FRM_ID_DATA,
                        m_out_data[m_out_processed].valid,
                        m_out_data[m_out_processed].data,
                        m_out_data[m_out_processed].id);
                }                
                m_out_processed++; // skip past this data.
            }
        }
        else
        {
            // optional raw output for debugging / monitor tools of unknown src ID data
            if(m_b_output_unpacked_raw)
            {
                outputRawMonBytes(  OCSD_OP_DATA, 
                    m_out_data[m_out_processed].index, 
                    OCSD_FRM_ID_DATA,
                    m_out_data[m_out_processed].valid,
                    m_out_data[m_out_processed].data,
                    m_out_data[m_out_processed].id);
            }             
            m_out_processed++; // skip past this data.
        }
    }
    return cont_processing;
}

/***************************************************************/
/* interface */
/***************************************************************/
TraceFormatterFrameDecoder::TraceFormatterFrameDecoder() : m_pDecoder(0)
{
    m_instNum = -1;
}

TraceFormatterFrameDecoder::TraceFormatterFrameDecoder(int instNum) : m_pDecoder(0)
{
    m_instNum = instNum;
}

TraceFormatterFrameDecoder::~TraceFormatterFrameDecoder()
{
    if(m_pDecoder) 
    {
        delete m_pDecoder;
        m_pDecoder = 0;
    }
}

    /* the data input interface from the reader / source */
ocsd_datapath_resp_t TraceFormatterFrameDecoder::TraceDataIn(  const ocsd_datapath_op_t op, 
                                                                const ocsd_trc_index_t index, 
                                                                const uint32_t dataBlockSize, 
                                                                const uint8_t *pDataBlock, 
                                                                uint32_t *numBytesProcessed)
{
    return (m_pDecoder == 0) ? OCSD_RESP_FATAL_NOT_INIT : m_pDecoder->TraceDataIn(op,index,dataBlockSize,pDataBlock,numBytesProcessed);
}

/* attach a data processor to a stream ID output */
componentAttachPt<ITrcDataIn> *TraceFormatterFrameDecoder::getIDStreamAttachPt(uint8_t ID)
{
    componentAttachPt<ITrcDataIn> *pAttachPt = 0;
    if((ID < 128) && (m_pDecoder != 0))
        pAttachPt = &(m_pDecoder->m_IDStreams[ID]);
    return pAttachPt;
}

/* attach a data processor to the raw frame output */
componentAttachPt<ITrcRawFrameIn> *TraceFormatterFrameDecoder::getTrcRawFrameAttachPt()
{
    return (m_pDecoder != 0) ? &m_pDecoder->m_RawTraceFrame : 0;
}


componentAttachPt<ITrcSrcIndexCreator> *TraceFormatterFrameDecoder::getTrcSrcIndexAttachPt()
{
    return (m_pDecoder != 0) ? &m_pDecoder->m_SrcIndexer : 0;
}

componentAttachPt<ITraceErrorLog> *TraceFormatterFrameDecoder::getErrLogAttachPt()
{
    return (m_pDecoder != 0) ? m_pDecoder->getErrorLogAttachPt() : 0;
}

/* configuration - set operational mode for incoming stream (has FSYNCS etc) */
ocsd_err_t TraceFormatterFrameDecoder::Configure(uint32_t cfg_flags)
{
    if(!m_pDecoder) 
    {  
        if(m_instNum >= 0)
            m_pDecoder = new (std::nothrow) TraceFmtDcdImpl(m_instNum);
        else
            m_pDecoder = new (std::nothrow) TraceFmtDcdImpl();
        if(!m_pDecoder) return OCSD_ERR_MEM;
    }
    m_pDecoder->m_cfgFlags = cfg_flags;
    return OCSD_OK;
}

const uint32_t TraceFormatterFrameDecoder::getConfigFlags() const
{
    uint32_t flags = 0;
    if(m_pDecoder)
        flags = m_pDecoder->m_cfgFlags;
    return flags;
}


/* enable / disable ID streams - default as all enabled */
ocsd_err_t TraceFormatterFrameDecoder::OutputFilterIDs(std::vector<uint8_t> &id_list, bool bEnable)
{
    return (m_pDecoder == 0) ? OCSD_ERR_NOT_INIT : m_pDecoder->OutputFilterIDs(id_list,bEnable);
}

ocsd_err_t TraceFormatterFrameDecoder::OutputFilterAllIDs(bool bEnable)
{
    return (m_pDecoder == 0) ? OCSD_ERR_NOT_INIT : m_pDecoder->OutputFilterAllIDs(bEnable);
}

/* decode control */
ocsd_datapath_resp_t TraceFormatterFrameDecoder::Reset()
{
    return (m_pDecoder == 0) ? OCSD_RESP_FATAL_NOT_INIT : m_pDecoder->Reset();
}

ocsd_datapath_resp_t TraceFormatterFrameDecoder::Flush()
{
    return (m_pDecoder == 0) ? OCSD_RESP_FATAL_NOT_INIT : m_pDecoder->Flush();
}


/* End of File trc_frame_deformatter.cpp */
