/*
 * \file       trc_indexer_pkt_i.h
 * \brief      OpenCSD : Trace packet indexer
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

#ifndef ARM_TRC_INDEXER_PKT_I_H_INCLUDED
#define ARM_TRC_INDEXER_PKT_I_H_INCLUDED

#include "trc_abs_typed_base_i.h"

/*!
 * @class ITrcPktIndexer 

 * @brief Templated interface class to index packet types. 
 *
 * @ingroup ocsd_interfaces
 * 
 * Each protocol version will have an associated indexer that will index significant 
 * packets such as synchronisation points, timestamps, trigger events.
 * 
 * Creating an index is optional at runtime, but will allow any analysis program to synchronise the 
 * different trace streams. 
 * 
 * Indexes need to be created only once and can be saved for re-use.
 * 
 * Packet processors should be created to support the attachment of an indexer.
 * 
 */
template <class Pt>
class ITrcPktIndexer : public ITrcTypedBase
{
public:
    ITrcPktIndexer() {}; /**< Default constructor. */
    virtual ~ITrcPktIndexer() {}; /**< Default destructor. */

    /*!
     * Interface method for trace packet indexing. Implementated by a channel packet indexer.
     *
     * @param index_sop : trace index at the start of the packet.
     * @param *packet_type : The packet type being indexed.
     */
    virtual void TracePktIndex(const ocsd_trc_index_t index_sop, const Pt *packet_type) = 0;
};

#endif // ARM_TRC_INDEXER_PKT_I_H_INCLUDED

/* End of File trc_indexer_pkt_i.h */
