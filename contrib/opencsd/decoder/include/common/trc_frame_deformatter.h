/*!
 * \file       trc_frame_deformatter.h
 * \brief      OpenCSD : De-format CoreSight formatted trace frame.
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
#ifndef ARM_TRC_FRAME_DEFORMATTER_H_INCLUDED
#define ARM_TRC_FRAME_DEFORMATTER_H_INCLUDED

#include "opencsd/ocsd_if_types.h"

#include "interfaces/trc_data_raw_in_i.h"
#include "comp_attach_pt_t.h"

class ITrcRawFrameIn;
class ITrcDataMixIDIn;
class ITrcSrcIndexCreator;
class ITraceErrorLog;
class TraceFmtDcdImpl;

/** @defgroup ocsd_deformatter  OpenCSD Library : Trace Frame Deformatter
    @brief CoreSight Formatted Trace Frame  - deformatting functionality.
@{*/

class TraceFormatterFrameDecoder : public ITrcDataIn
{
public:
    TraceFormatterFrameDecoder();
    TraceFormatterFrameDecoder(int instNum);
    virtual ~TraceFormatterFrameDecoder();

    /* the data input interface from the reader */
    virtual ocsd_datapath_resp_t TraceDataIn(  const ocsd_datapath_op_t op, 
                                                const ocsd_trc_index_t index, 
                                                const uint32_t dataBlockSize, 
                                                const uint8_t *pDataBlock, 
                                                uint32_t *numBytesProcessed);

    /* attach a data processor to a stream ID output */
    componentAttachPt<ITrcDataIn> *getIDStreamAttachPt(uint8_t ID);

    /* attach a data processor to the raw frame output */
    componentAttachPt<ITrcRawFrameIn> *getTrcRawFrameAttachPt();

    componentAttachPt<ITrcSrcIndexCreator> *getTrcSrcIndexAttachPt();

    componentAttachPt<ITraceErrorLog> *getErrLogAttachPt();

    /* configuration - set operational mode for incoming stream (has FSYNCS etc) */
    ocsd_err_t Configure(uint32_t cfg_flags);
    const uint32_t getConfigFlags() const;

    /* enable / disable ID streams - default as all enabled */
    ocsd_err_t OutputFilterIDs(std::vector<uint8_t> &id_list, bool bEnable);
    ocsd_err_t OutputFilterAllIDs(bool bEnable);

    /* decode control */
    ocsd_datapath_resp_t Reset();    /* reset the decode to the start state, drop partial data - propogate to attached components */
    ocsd_datapath_resp_t Flush();    /* flush existing data if possible, retain state - propogate to attached components */

private:
    TraceFmtDcdImpl *m_pDecoder;
    int m_instNum;
};

/** @}*/

#endif // ARM_TRC_FRAME_DEFORMATTER_H_INCLUDED

/* End of File trc_frame_deformatter.h */