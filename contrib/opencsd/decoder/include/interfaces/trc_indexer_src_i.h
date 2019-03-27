/*
 * \file       trc_indexer_src_i.h
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


#ifndef ARM_TRC_INDEXER_SRC_I_H_INCLUDED
#define ARM_TRC_INDEXER_SRC_I_H_INCLUDED

#include <vector>
#include "opencsd/ocsd_if_types.h"

/*!
 * @class ITrcSrcIndexCreator   
 * 
 * @brief Interface class to index the frame formatted trace stream
 *
 * @ingroup ocsd_interfaces
 * 
 * This indexer creates an index of trace IDs present in the frame formatted trace stream.
 * It will also index any trigger point markers indicated in the frame format.
 *
 * Indexing is optional at runtime. Indexes can be saved and re-used.
 */
class ITrcSrcIndexCreator 
{
public:
    ITrcSrcIndexCreator() {};   /**< Default constructor. */
    virtual ~ITrcSrcIndexCreator() {};  /**< Default destructor. */

    /*!
     * The size of block that the indexer will split trace into - this is effectively the 
     * index granularity. The indexing will indicate if an indexed element - e.g. a source
     * ID - is present in the block. Smaller granularity will mean a larger index but more
     * resolution in IDs and event positions.
     *
     * Block sizes will be power of 2 aligned, not less 256 bytes (16 frames).
     * Indexer will choose block size based on total trace size and desired granularity.
     * 
     * @return uint32_t : Size of indexing block.
     */
    virtual const uint32_t IndexBlockSize() const;

    /*!
     * Index a single ID
     *
     * @param src_idx : trace index of source ID
     * @param ID : The source ID.
     *
     * @return virtual ocsd_err_t  : OCSD_OK if successful.
     */
    virtual ocsd_err_t TrcIDIndex(const ocsd_trc_index_t src_idx, const uint8_t ID) = 0;
    
    /*!
     * Index a set of IDs in a block.
     * Block is assumed to be one of size IndexBlockSize()
     *
     * May be used by the deformatter to collate IDs and reduce indexing calls.
     * May be used by hardware capture source that has its own index of IDs, to transfer
     * indexing information into the decoder indexer.
     *
     * @param src_idx_start : Index of start of block.
     * @param IDs : IDs within the block.
     *
     * @return virtual ocsd_err_t  : OCSD_OK if successful.
     */
    virtual ocsd_err_t TrcIDBlockMap(const ocsd_trc_index_t src_idx_start, const std::vector<uint8_t> IDs) = 0;
       
    /*!
     * The CoreSight frame format can use a reserved ID to indicate trigger or other 
     * events programmed into the trace protocol generator. 
     * This call indexes these events.
     *
     * @param src_idx : trace index of the event.
     * @param event_type : type of event.
     *
     * @return ocsd_err_t  : OCSD_OK if indexed correctly,  OCSD_ERR_INVALID_PARAM_VAL if incorrect value used.
     */
    virtual ocsd_err_t TrcEventIndex(const ocsd_trc_index_t src_idx, const int event_type) = 0;

    
    /*!
     * When the frame formatter is using frame syncs (typically TPIU output captured on off chip capture
     * device), this index call notes the position of these elements.
     *
     * @param src_idx : trace index of sync point.
     */
    virtual void TrcSyncIndex(const ocsd_trc_index_t src_idx);

};

#endif // ARM_TRC_INDEXER_SRC_I_H_INCLUDED

/* End of File trc_indexer_src_i.h */
